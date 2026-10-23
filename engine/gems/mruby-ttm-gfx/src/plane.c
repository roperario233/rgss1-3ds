/*
 * plane.c -- RGSS1 Plane: a bitmap repeated across its viewport.
 *
 * The game has two, both in Spriteset_Map: the panorama (z -1000, behind the
 * map, scrolled at half the map's speed) and the fog (z 3000, zoomed, faded,
 * blended). The bitmap is tiled over the whole viewport (or the screen),
 * scaled by zoom_x/zoom_y, and shifted by ox/oy -- screen pixels, wrapping --
 * plus the viewport's own ox/oy, so a screen shake moves it with the map.
 * Opacity, blend type, color and tone work as on a Sprite (the game tints
 * its fogs with fog_tone). Each tile goes through ttm_bitmap_draw, so a
 * panorama wider than the texture limit (NightCliff, 1645 px) just works, and
 * ttm_draw drops the pieces outside the viewport.
 */

#include <math.h>
#include <stdlib.h>

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/variable.h>

#include "gfx.h"

typedef struct {
    ttm_drawable d;
    int ox, oy;
    double zoom_x, zoom_y;
    int opacity, blend_type;
} ttm_plane;

#define IV(name) mrb_intern_lit(mrb, name)

static void plane_free(mrb_state* mrb, void* p)
{
    ttm_plane* pl = (ttm_plane*)p;
    if (!pl) return;
    if (!pl->d.disposed) ttm_unregister(&pl->d);
    free(pl);
}

static const mrb_data_type plane_type = { "Plane", plane_free };

static ttm_plane* get(mrb_state* mrb, mrb_value self)
{
    ttm_plane* pl = (ttm_plane*)mrb_data_get_ptr(mrb, self, &plane_type);
    if (!pl) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "uninitialized plane");
    return pl;
}

/* ---- drawing --------------------------------------------------------------------- */

void ttm_plane_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int vox, int voy)
{
    ttm_plane* pl = (ttm_plane*)d;
    if (!d->visible || pl->opacity <= 0 || pl->zoom_x <= 0 || pl->zoom_y <= 0) return;
    mrb_value self = mrb_obj_value(d->self);
    ttm_bitmap* bm = ttm_bitmap_ptr(mrb_iv_get(mrb, self, IV("@bitmap")));
    if (!bm || bm->w <= 0 || bm->h <= 0) return;

    uint8_t tint[4] = { 0, 0, 0, 0 };
    ttm_color_bytes(mrb_iv_get(mrb, self, IV("@color")), tint);

    /* one tile's size on screen, and where the tiling starts: at or left of
     * (and at or above) the area to cover, ox/oy wrapping within a tile */
    double tw = bm->w * pl->zoom_x, th = bm->h * pl->zoom_y;
    int x0, y0, x1, y1;
    ttm_clip_current(&x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return;
    double sx = fmod((double)(pl->ox + vox), tw), sy = fmod((double)(pl->oy + voy), th);
    if (sx < 0) sx += tw;
    if (sy < 0) sy += th;
    double startx = vx - sx, starty = vy - sy;
    while (startx > x0) startx -= tw;
    while (starty > y0) starty -= th;

    int16_t tone[4];
    int toned = ttm_tone_values(mrb_iv_get(mrb, self, IV("@tone")), tone);
    if (toned) gfx_quad_tone(tone);
    for (double y = starty; y < y1; y += th) {
        for (double x = startx; x < x1; x += tw) {
            gfx_q q = { 0, 0, (float)bm->w, (float)bm->h, (float)x, (float)y, (float)tw, (float)th, 0, 0 };
            ttm_bitmap_draw(bm, q, pl->opacity, pl->blend_type, tint);
        }
    }
    if (toned) gfx_quad_tone(NULL);
}

/* ---- the Ruby class --------------------------------------------------------------- */

static mrb_value plane_init(mrb_state* mrb, mrb_value self)
{
    mrb_value viewport = mrb_nil_value();
    mrb_get_args(mrb, "|o", &viewport);
    ttm_plane* pl = (ttm_plane*)calloc(1, sizeof(ttm_plane));
    pl->zoom_x = pl->zoom_y = 1.0;
    pl->opacity = 255;
    mrb_data_init(self, pl, &plane_type);
    ttm_register(&pl->d, TTM_PLANE, (struct RData*)mrb_obj_ptr(self));
    mrb_iv_set(mrb, self, IV("@viewport"), viewport);
    return self;
}

static mrb_value plane_dispose(mrb_state* mrb, mrb_value self)
{
    ttm_plane* pl = get(mrb, self);
    if (!pl->d.disposed) ttm_unregister(&pl->d);
    return mrb_nil_value();
}
static mrb_value plane_disposed(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->d.disposed); }

#define OBJ_PROP(name, iv)                                                           \
    static mrb_value pl_get_##name(mrb_state* mrb, mrb_value self)                  \
    { return mrb_iv_get(mrb, self, IV(iv)); }                                         \
    static mrb_value pl_set_##name(mrb_state* mrb, mrb_value self)                  \
    { mrb_value v; mrb_get_args(mrb, "o", &v); mrb_iv_set(mrb, self, IV(iv), v); return v; }
OBJ_PROP(bitmap, "@bitmap")
OBJ_PROP(viewport, "@viewport")

/* color and tone: made on first read, as RGSS hands out a live object */
static mrb_value lazy_iv(mrb_state* mrb, mrb_value self, const char* iv, const char* klass)
{
    mrb_sym s = mrb_intern_cstr(mrb, iv);
    mrb_value v = mrb_iv_get(mrb, self, s);
    if (mrb_nil_p(v)) {
        v = mrb_funcall(mrb, mrb_obj_value(mrb_class_get(mrb, klass)), "new", 4, mrb_fixnum_value(0),
                        mrb_fixnum_value(0), mrb_fixnum_value(0), mrb_fixnum_value(0));
        mrb_iv_set(mrb, self, s, v);
    }
    return v;
}
static mrb_value pl_get_color(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@color", "Color"); }
static mrb_value pl_get_tone(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@tone", "Tone"); }
static mrb_value pl_set_color(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    mrb_iv_set(mrb, self, IV("@color"), v);
    return v;
}
static mrb_value pl_set_tone(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    mrb_iv_set(mrb, self, IV("@tone"), v);
    return v;
}

#define NUM_PROP(name, getter, setter)                                               \
    static mrb_value pl_get_##name(mrb_state* mrb, mrb_value self)                  \
    { ttm_plane* p = get(mrb, self); return getter; }                                 \
    static mrb_value pl_set_##name(mrb_state* mrb, mrb_value self)                  \
    { mrb_value v; mrb_get_args(mrb, "o", &v); ttm_plane* p = get(mrb, self); setter; return v; }

static int clamp255(mrb_int v) { return v < 0 ? 0 : v > 255 ? 255 : (int)v; }

NUM_PROP(z, mrb_int_value(mrb, p->d.z), ttm_set_z(&p->d, (int)ttm_to_int(mrb, v)))
NUM_PROP(ox, mrb_int_value(mrb, p->ox), p->ox = (int)ttm_to_int(mrb, v))
NUM_PROP(oy, mrb_int_value(mrb, p->oy), p->oy = (int)ttm_to_int(mrb, v))
NUM_PROP(zoom_x, mrb_float_value(mrb, p->zoom_x), p->zoom_x = ttm_to_dbl(mrb, v))
NUM_PROP(zoom_y, mrb_float_value(mrb, p->zoom_y), p->zoom_y = ttm_to_dbl(mrb, v))
NUM_PROP(opacity, mrb_int_value(mrb, p->opacity), p->opacity = clamp255(ttm_to_int(mrb, v)))
NUM_PROP(blend_type, mrb_int_value(mrb, p->blend_type), p->blend_type = (int)ttm_to_int(mrb, v))
NUM_PROP(visible, mrb_bool_value(p->d.visible), p->d.visible = mrb_test(v))

void ttm_init_plane(mrb_state* mrb)
{
    struct RClass* c = mrb_define_class(mrb, "Plane", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_method(mrb, c, "initialize", plane_init, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, c, "dispose", plane_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", plane_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "color", pl_get_color, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "color=", pl_set_color, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "tone", pl_get_tone, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "tone=", pl_set_tone, MRB_ARGS_REQ(1));
#define DEF(name)                                                                       \
    mrb_define_method(mrb, c, #name, pl_get_##name, MRB_ARGS_NONE());                   \
    mrb_define_method(mrb, c, #name "=", pl_set_##name, MRB_ARGS_REQ(1));
    DEF(bitmap) DEF(viewport) DEF(z) DEF(ox) DEF(oy) DEF(zoom_x) DEF(zoom_y)
    DEF(opacity) DEF(blend_type) DEF(visible)
#undef DEF
}
