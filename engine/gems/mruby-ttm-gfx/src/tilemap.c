/*
 * tilemap.c -- RGSS1 Tilemap: the map's tiles, drawn by the engine.
 *
 * map_data is a Table (x, y, layer 0..2) of tile ids:
 *   0..47     nothing
 *   48..383   autotiles: autotiles[id / 48 - 1], pattern id % 48
 *   384..     the tileset, eight tiles to a row: ((id-384) % 8, (id-384) / 8)
 * priorities (a Table by tile id) says how a tile sits among the characters.
 *
 * z order, as in RGSS: priority-0 tiles are under everything (z 0). A tile of
 * priority p on map row y is at z = (y*32 - oy) + 32 + 32p, so a character
 * (Game_Character#screen_z: its screen row + 32, +31 if taller than a tile)
 * passes in front of or behind it by where it stands. Each priority tile
 * needs its own z, so besides the ground the tilemap registers one drawable
 * per screen row -- a "row layer" at z = row*32 - oy + 64 -- and a priority-p
 * tile on row y goes into the layer of row y + p - 1, which has exactly that
 * z. Layers keep their creation order when their z moves with oy (they are
 * set directly, not through ttm_set_z), so on a tie the character sprites,
 * created after the tilemap, draw on top -- as RGSS does.
 *
 * Only the visible cells are drawn (21 x 16), straight from the tileset
 * through ttm_bitmap_draw, so a 256x9332 tileset's slices just work.
 * Autotiles: each is turned once into a 256-wide bitmap with its 48 patterns
 * (8 x 6 tiles) per animation frame, assembled from 16 px quarters with RMXP's
 * pattern table; a 32 px high autotile is a plain tile animated sideways.
 * update advances the animation one frame per 16 calls.
 * flash_data is kept but not drawn.
 */

#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/variable.h>

#include "gfx.h"

#define TM_ROWS 20       /* row layers: screen rows 0..15 plus priorities up to 5 */
#define TM_COLS 21       /* visible columns: 640 / 32 + a partial one */
#define TM_VROWS 16      /* visible rows: 480 / 32 + a partial one */

typedef struct ttm_tilemap ttm_tilemap;

typedef struct {
    ttm_drawable d;
    ttm_tilemap* tm;
    int k;               /* row layer index; -1 for the ground */
} tm_layer;

typedef struct {
    uint32_t serial;     /* the autotile bitmap the patterns were made from */
    int w, h;
    ttm_bitmap* patterns;/* NULL for a flat (32 px high) autotile */
    int frames;
} tm_autotile;

struct ttm_tilemap {
    tm_layer ground;
    tm_layer rows[TM_ROWS];
    int ox, oy, visible, disposed;
    mrb_int anim;
    tm_autotile at[7];
    /* what the ivars held when this frame was first drawn */
    uint32_t resolved;
    ttm_bitmap* tileset;
    ttm_bitmap* autos[7];
    ttm_table_t* map;
    ttm_table_t* prio;
};

#define IV(name) mrb_intern_lit(mrb, name)

/* RMXP's autotile patterns: for each of the 48, the four 16 px quarters
 * (top-left, top-right, bottom-left, bottom-right), numbered 1.. across the
 * 96x128 sheet in rows of six */
static const uint8_t AUTOTILE[48][4] = {
    {27, 28, 33, 34}, { 5, 28, 33, 34}, {27,  6, 33, 34}, { 5,  6, 33, 34},
    {27, 28, 33, 12}, { 5, 28, 33, 12}, {27,  6, 33, 12}, { 5,  6, 33, 12},
    {27, 28, 11, 34}, { 5, 28, 11, 34}, {27,  6, 11, 34}, { 5,  6, 11, 34},
    {27, 28, 11, 12}, { 5, 28, 11, 12}, {27,  6, 11, 12}, { 5,  6, 11, 12},
    {25, 26, 31, 32}, {25,  6, 31, 32}, {25, 26, 31, 12}, {25,  6, 31, 12},
    {15, 16, 21, 22}, {15, 16, 21, 12}, {15, 16, 11, 22}, {15, 16, 11, 12},
    {29, 30, 35, 36}, {29, 30, 11, 36}, { 5, 30, 35, 36}, { 5, 30, 11, 36},
    {39, 40, 45, 46}, { 5, 40, 45, 46}, {39,  6, 45, 46}, { 5,  6, 45, 46},
    {25, 30, 31, 36}, {15, 16, 45, 46}, {13, 14, 19, 20}, {13, 14, 19, 12},
    {17, 18, 23, 24}, {17, 18, 11, 24}, {41, 42, 47, 48}, { 5, 42, 47, 48},
    {37, 38, 43, 44}, {37,  6, 43, 44}, {13, 18, 19, 24}, {13, 14, 43, 44},
    {37, 42, 43, 48}, {17, 18, 47, 48}, {13, 18, 43, 48}, { 1,  2,  7,  8},
};

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int wrap(int a, int n) { int m = a % n; return m < 0 ? m + n : m; }

/* ---- autotiles ------------------------------------------------------------------ */

static void autotile_clear(tm_autotile* at)
{
    if (at->patterns) ttm_bitmap_destroy(at->patterns);
    memset(at, 0, sizeof(*at));
}

static void autotile_prepare(tm_autotile* at, ttm_bitmap* src)
{
    if (!src) {
        autotile_clear(at);
        return;
    }
    if (at->serial == src->serial && at->w == src->w && at->h == src->h) return;
    autotile_clear(at);
    if (!ttm_bitmap_pixels(src)) return;    /* shed and not decodable now: next frame */
    at->serial = src->serial;
    at->w = src->w;
    at->h = src->h;
    if (src->h < 128 || src->w < 96) {           /* flat: one tile per frame */
        at->frames = src->w / 32 > 0 ? src->w / 32 : 1;
        return;
    }
    at->frames = src->w / 96;
    at->patterns = ttm_bitmap_new_raw(256, 192 * at->frames);
    if (!at->patterns) return;
    for (int f = 0; f < at->frames; ++f)
        for (int p = 0; p < 48; ++p)
            for (int q = 0; q < 4; ++q) {
                int n = AUTOTILE[p][q] - 1;
                int sx = f * 96 + (n % 6) * 16, sy = (n / 6) * 16;
                int dx = (p % 8) * 32 + (q % 2) * 16, dy = f * 192 + (p / 8) * 32 + (q / 2) * 16;
                for (int r = 0; r < 16; ++r)
                    memcpy(ttm_row(at->patterns, dy + r) + dx, ttm_row(src, sy + r) + sx, 16 * 4);
            }
}

/* ---- drawing ---------------------------------------------------------------------- */

static void resolve(mrb_state* mrb, ttm_tilemap* tm, struct RData* self)
{
    if (tm->resolved == ttm_render_frame) return;
    tm->resolved = ttm_render_frame;
    mrb_value obj = mrb_obj_value(self);
    tm->tileset = ttm_bitmap_ptr(mrb_iv_get(mrb, obj, IV("@tileset")));
    tm->map = ttm_table_ptr(mrb_iv_get(mrb, obj, IV("@map_data")));
    tm->prio = ttm_table_ptr(mrb_iv_get(mrb, obj, IV("@priorities")));
    mrb_value ats = mrb_iv_get(mrb, obj, IV("@autotiles"));
    for (int i = 0; i < 7; ++i) {
        mrb_value v = mrb_array_p(ats) && i < RARRAY_LEN(ats) ? RARRAY_PTR(ats)[i] : mrb_nil_value();
        tm->autos[i] = ttm_bitmap_ptr(v);
        autotile_prepare(&tm->at[i], tm->autos[i]);
    }
}

static int priority_of(const ttm_tilemap* tm, int id)
{
    if (!tm->prio || id < 0 || id >= tm->prio->xs) return 0;
    return tm->prio->data[id];
}

static const uint8_t no_tint[4] = { 0, 0, 0, 0 };

static void draw_tile(ttm_tilemap* tm, int id, int x, int y)
{
    gfx_q q = { 0, 0, 32, 32, (float)x, (float)y, 32, 32, 0, 0 };
    ttm_bitmap* b;
    if (id >= 384) {
        b = tm->tileset;
        if (!b) return;
        q.sx = (float)((id - 384) % 8 * 32);
        q.sy = (float)((id - 384) / 8 * 32);
        if (q.sy >= b->h) return;
    } else if (id >= 48) {
        int a = id / 48 - 1;
        tm_autotile* at = &tm->at[a];
        if (!tm->autos[a] || !at->frames) return;
        int f = (int)((tm->anim / 16) % at->frames);
        if (at->patterns) {
            b = at->patterns;
            q.sx = (float)(id % 48 % 8 * 32);
            q.sy = (float)(f * 192 + id % 48 / 8 * 32);
        } else {
            b = tm->autos[a];
            q.sx = (float)(f * 32);
        }
    } else {
        return;
    }
    ttm_bitmap_draw(b, q, 255, 0, no_tint);
}

/* draw the tiles of map row `row` whose priority is `p` (0: the ground) */
static void draw_row(ttm_tilemap* tm, int row, int p, int vx, int vy, int ax, int ay)
{
    ttm_table_t* m = tm->map;
    int sy = vy + row * 32 - ay;
    if (sy <= -32 || sy >= TTM_SCREEN_H) return;
    int my = wrap(row, m->ys);
    int x0 = floordiv(ax, 32);
    for (int c = 0; c < TM_COLS; ++c) {
        int col = x0 + c;
        int mx = wrap(col, m->xs);
        int sx = vx + col * 32 - ax;
        for (int z = 0; z < m->zs && z < 3; ++z) {
            int id = m->data[((size_t)z * m->ys + my) * m->xs + mx];
            if (id < 48) continue;
            int pr = priority_of(tm, id);
            if (p == 0 ? pr != 0 : pr != p) continue;
            draw_tile(tm, id, sx, sy);
        }
    }
}

void ttm_tilemap_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int vox, int voy)
{
    tm_layer* layer = (tm_layer*)d;
    ttm_tilemap* tm = layer->tm;
    if (tm->disposed || !tm->visible) return;
    resolve(mrb, tm, d->self);
    ttm_table_t* m = tm->map;
    if (!m || m->xs <= 0 || m->ys <= 0) return;
    int ax = tm->ox + vox, ay = tm->oy + voy;
    if (layer->k < 0) {
        int y0 = floordiv(ay, 32);
        for (int r = 0; r < TM_VROWS; ++r) draw_row(tm, y0 + r, 0, vx, vy, ax, ay);
    } else {
        int rowk = floordiv(tm->oy, 32) + layer->k;
        for (int p = 1; p <= 5; ++p) draw_row(tm, rowk - p + 1, p, vx, vy, ax, ay);
    }
}

/* the row layers' z follows oy: row k's layer is at row*32 - oy + 64 */
static void place(ttm_tilemap* tm)
{
    int top = floordiv(tm->oy, 32);
    tm->ground.d.z = 0;
    for (int k = 0; k < TM_ROWS; ++k) tm->rows[k].d.z = (top + k) * 32 - tm->oy + 64;
}

/* ---- the Ruby class --------------------------------------------------------------- */

static void unregister_all(ttm_tilemap* tm)
{
    if (tm->disposed) return;
    tm->disposed = 1;
    ttm_unregister(&tm->ground.d);
    for (int k = 0; k < TM_ROWS; ++k) ttm_unregister(&tm->rows[k].d);
    for (int i = 0; i < 7; ++i) autotile_clear(&tm->at[i]);
}

static void tilemap_free(mrb_state* mrb, void* p)
{
    ttm_tilemap* tm = (ttm_tilemap*)p;
    if (!tm) return;
    unregister_all(tm);
    free(tm);
}

static const mrb_data_type tilemap_type = { "Tilemap", tilemap_free };

static ttm_tilemap* get(mrb_state* mrb, mrb_value self)
{
    ttm_tilemap* tm = (ttm_tilemap*)mrb_data_get_ptr(mrb, self, &tilemap_type);
    if (!tm) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "uninitialized tilemap");
    return tm;
}

static mrb_value tilemap_init(mrb_state* mrb, mrb_value self)
{
    mrb_value viewport = mrb_nil_value();
    mrb_get_args(mrb, "|o", &viewport);
    ttm_tilemap* tm = (ttm_tilemap*)calloc(1, sizeof(ttm_tilemap));
    tm->visible = 1;
    mrb_data_init(self, tm, &tilemap_type);
    struct RData* rd = (struct RData*)mrb_obj_ptr(self);
    tm->ground.tm = tm;
    tm->ground.k = -1;
    ttm_register(&tm->ground.d, TTM_TILEMAP_GROUND, rd);
    for (int k = 0; k < TM_ROWS; ++k) {
        tm->rows[k].tm = tm;
        tm->rows[k].k = k;
        ttm_register(&tm->rows[k].d, TTM_TILEMAP_ROW, rd);
    }
    place(tm);
    mrb_iv_set(mrb, self, IV("@viewport"), viewport);
    mrb_value ats = mrb_ary_new_capa(mrb, 7);
    for (int i = 0; i < 7; ++i) mrb_ary_push(mrb, ats, mrb_nil_value());
    mrb_iv_set(mrb, self, IV("@autotiles"), ats);
    return self;
}

static mrb_value tilemap_dispose(mrb_state* mrb, mrb_value self)
{
    unregister_all(get(mrb, self));
    return mrb_nil_value();
}
static mrb_value tilemap_disposed(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->disposed); }

static mrb_value tilemap_update(mrb_state* mrb, mrb_value self)
{
    get(mrb, self)->anim++;
    return mrb_nil_value();
}

#define OBJ_PROP(name, iv)                                                           \
    static mrb_value tm_get_##name(mrb_state* mrb, mrb_value self)                  \
    { return mrb_iv_get(mrb, self, IV(iv)); }                                         \
    static mrb_value tm_set_##name(mrb_state* mrb, mrb_value self)                  \
    { mrb_value v; mrb_get_args(mrb, "o", &v); mrb_iv_set(mrb, self, IV(iv), v); return v; }
OBJ_PROP(tileset, "@tileset")
OBJ_PROP(autotiles, "@autotiles")
OBJ_PROP(map_data, "@map_data")
OBJ_PROP(priorities, "@priorities")
OBJ_PROP(flash_data, "@flash_data")
OBJ_PROP(viewport, "@viewport")

static mrb_value tm_get_ox(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, get(mrb, self)->ox); }
static mrb_value tm_get_oy(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, get(mrb, self)->oy); }
static mrb_value tm_get_visible(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->visible); }
static mrb_value tm_set_ox(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    get(mrb, self)->ox = (int)ttm_to_int(mrb, v);
    return v;
}
static mrb_value tm_set_oy(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    ttm_tilemap* tm = get(mrb, self);
    tm->oy = (int)ttm_to_int(mrb, v);
    place(tm);
    return v;
}
static mrb_value tm_set_visible(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    get(mrb, self)->visible = mrb_test(v);
    return v;
}

void ttm_init_tilemap(mrb_state* mrb)
{
    struct RClass* c = mrb_define_class(mrb, "Tilemap", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_method(mrb, c, "initialize", tilemap_init, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, c, "dispose", tilemap_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", tilemap_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "update", tilemap_update, MRB_ARGS_NONE());
#define DEF(name)                                                                       \
    mrb_define_method(mrb, c, #name, tm_get_##name, MRB_ARGS_NONE());                   \
    mrb_define_method(mrb, c, #name "=", tm_set_##name, MRB_ARGS_REQ(1));
    DEF(tileset) DEF(autotiles) DEF(map_data) DEF(priorities) DEF(flash_data)
    DEF(viewport) DEF(ox) DEF(oy) DEF(visible)
#undef DEF
}
