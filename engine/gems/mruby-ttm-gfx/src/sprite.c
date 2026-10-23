/*
 * sprite.c -- RGSS1 Sprite.
 *
 * Drawing: the source rectangle of the bitmap is placed with its origin
 * (ox, oy) at (x, y), scaled by zoom_x/zoom_y, rotated by angle degrees
 * counter-clockwise, optionally mirrored, faded by opacity, tinted by color and
 * by a running flash, and its bottom bush_depth rows drawn at half opacity
 * (characters wading through bushes). Tone is kept but not drawn yet.
 * bitmap, src_rect, viewport, color and tone are ivars, so the GC sees them.
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
    int x, y, ox, oy;
    double zoom_x, zoom_y, angle;
    int mirror, bush_depth, opacity, blend_type;
    uint8_t flash[4];
    int flash_hide, flash_count, flash_duration;
} ttm_sprite;

static void sprite_free(mrb_state* mrb, void* p)
{
    ttm_sprite* s = (ttm_sprite*)p;
    if (!s) return;
    if (!s->d.disposed) ttm_unregister(&s->d);
    free(s);
}

static const mrb_data_type sprite_type = { "Sprite", sprite_free };

static ttm_sprite* get(mrb_state* mrb, mrb_value self)
{
    ttm_sprite* s = (ttm_sprite*)mrb_data_get_ptr(mrb, self, &sprite_type);
    if (!s) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "uninitialized sprite");
    return s;
}

#define IV(name) mrb_intern_lit(mrb, name)

static mrb_value sprite_init(mrb_state* mrb, mrb_value self)
{
    mrb_value viewport = mrb_nil_value();
    mrb_get_args(mrb, "|o", &viewport);
    ttm_sprite* s = (ttm_sprite*)calloc(1, sizeof(ttm_sprite));
    s->zoom_x = s->zoom_y = 1.0;
    s->opacity = 255;
    mrb_data_init(self, s, &sprite_type);
    ttm_register(&s->d, TTM_SPRITE, (struct RData*)mrb_obj_ptr(self));
    mrb_iv_set(mrb, self, IV("@viewport"), viewport);
    mrb_iv_set(mrb, self, IV("@src_rect"), ttm_rect_new(mrb, 0, 0, 0, 0));
    return self;
}

static mrb_value sprite_dispose(mrb_state* mrb, mrb_value self)
{
    ttm_sprite* s = get(mrb, self);
    if (!s->d.disposed) ttm_unregister(&s->d);
    return mrb_nil_value();
}
static mrb_value sprite_disposed(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->d.disposed); }

/* bitmap: setting one resets src_rect to the whole bitmap, as RGSS does */
static mrb_value sprite_bitmap(mrb_state* mrb, mrb_value self) { return mrb_iv_get(mrb, self, IV("@bitmap")); }
static mrb_value sprite_set_bitmap(mrb_state* mrb, mrb_value self)
{
    mrb_value b;
    mrb_get_args(mrb, "o", &b);
    get(mrb, self);
    mrb_iv_set(mrb, self, IV("@bitmap"), b);
    ttm_bitmap* bm = ttm_bitmap_ptr(b);
    if (bm) {
        ttm_rect_t* r = ttm_rect_ptr(mrb_iv_get(mrb, self, IV("@src_rect")));
        if (r) { r->x = 0; r->y = 0; r->w = bm->w; r->h = bm->h; }
    }
    return b;
}

#define OBJ_PROP(name, iv)                                                           \
    static mrb_value sprite_get_##name(mrb_state* mrb, mrb_value self)              \
    { return mrb_iv_get(mrb, self, IV(iv)); }                                         \
    static mrb_value sprite_set_##name(mrb_state* mrb, mrb_value self)              \
    { mrb_value v; mrb_get_args(mrb, "o", &v); mrb_iv_set(mrb, self, IV(iv), v); return v; }
OBJ_PROP(src_rect, "@src_rect")
OBJ_PROP(viewport, "@viewport")

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
static mrb_value sprite_color(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@color", "Color"); }
static mrb_value sprite_tone(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@tone", "Tone"); }
static mrb_value sprite_set_color(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    mrb_iv_set(mrb, self, IV("@color"), v);
    return v;
}
static mrb_value sprite_set_tone(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    mrb_iv_set(mrb, self, IV("@tone"), v);
    return v;
}

#define NUM_PROP(name, get_expr, set_stmt)                                              \
    static mrb_value sprite_get_##name(mrb_state* mrb, mrb_value self)                 \
    { ttm_sprite* s = get(mrb, self); return get_expr; }                                 \
    static mrb_value sprite_set_##name(mrb_state* mrb, mrb_value self)                 \
    { mrb_value v; mrb_get_args(mrb, "o", &v); ttm_sprite* s = get(mrb, self); set_stmt; return v; }

NUM_PROP(x, mrb_int_value(mrb, s->x), s->x = (int)ttm_to_int(mrb, v))
NUM_PROP(y, mrb_int_value(mrb, s->y), s->y = (int)ttm_to_int(mrb, v))
NUM_PROP(z, mrb_int_value(mrb, s->d.z), ttm_set_z(&s->d, (int)ttm_to_int(mrb, v)))
NUM_PROP(ox, mrb_int_value(mrb, s->ox), s->ox = (int)ttm_to_int(mrb, v))
NUM_PROP(oy, mrb_int_value(mrb, s->oy), s->oy = (int)ttm_to_int(mrb, v))
NUM_PROP(zoom_x, mrb_float_value(mrb, s->zoom_x), s->zoom_x = ttm_to_dbl(mrb, v))
NUM_PROP(zoom_y, mrb_float_value(mrb, s->zoom_y), s->zoom_y = ttm_to_dbl(mrb, v))
NUM_PROP(angle, mrb_float_value(mrb, s->angle), s->angle = ttm_to_dbl(mrb, v))
NUM_PROP(mirror, mrb_bool_value(s->mirror), s->mirror = mrb_test(v))
NUM_PROP(bush_depth, mrb_int_value(mrb, s->bush_depth), s->bush_depth = (int)ttm_to_int(mrb, v))
NUM_PROP(blend_type, mrb_int_value(mrb, s->blend_type), s->blend_type = (int)ttm_to_int(mrb, v))
NUM_PROP(visible, mrb_bool_value(s->d.visible), s->d.visible = mrb_test(v))
NUM_PROP(opacity, mrb_int_value(mrb, s->opacity),
         { mrb_int o = ttm_to_int(mrb, v); s->opacity = o < 0 ? 0 : o > 255 ? 255 : (int)o; })

static mrb_value sprite_flash(mrb_state* mrb, mrb_value self)
{
    mrb_value color;
    mrb_int duration;
    mrb_get_args(mrb, "oi", &color, &duration);
    ttm_sprite* s = get(mrb, self);
    s->flash_hide = mrb_nil_p(color);
    ttm_color_bytes(color, s->flash);
    s->flash_duration = s->flash_count = duration > 0 ? (int)duration : 0;
    return mrb_nil_value();
}

static mrb_value sprite_update(mrb_state* mrb, mrb_value self)
{
    ttm_sprite* s = get(mrb, self);
    if (s->flash_count > 0) s->flash_count--;
    return mrb_nil_value();
}

/* ---- drawing --------------------------------------------------------------------- */

void ttm_sprite_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int vox, int voy)
{
    ttm_sprite* s = (ttm_sprite*)d;
    if (!d->visible || s->opacity == 0) return;
    if (s->flash_count > 0 && s->flash_hide) return;
    mrb_value self = mrb_obj_value(d->self);
    ttm_bitmap* bm = ttm_bitmap_ptr(mrb_iv_get(mrb, self, IV("@bitmap")));
    ttm_rect_t* r = ttm_rect_ptr(mrb_iv_get(mrb, self, IV("@src_rect")));
    if (!bm || !r) return;

    /* source rect clipped to the bitmap; keep the origin where the script put it */
    int sx = (int)r->x, sy = (int)r->y, sw = (int)r->w, sh = (int)r->h;
    int ox = s->ox, oy = s->oy;
    if (sx < 0) { ox += sx; sw += sx; sx = 0; }
    if (sy < 0) { oy += sy; sh += sy; sy = 0; }
    if (sx + sw > bm->w) sw = bm->w - sx;
    if (sy + sh > bm->h) sh = bm->h - sy;
    if (sw <= 0 || sh <= 0) return;

    uint8_t tint[4] = { 0, 0, 0, 0 };
    ttm_color_bytes(mrb_iv_get(mrb, self, IV("@color")), tint);
    if (s->flash_count > 0 && s->flash_duration > 0) {
        int fa = s->flash[3] * s->flash_count / s->flash_duration;
        if (fa > tint[3]) {
            tint[0] = s->flash[0]; tint[1] = s->flash[1]; tint[2] = s->flash[2]; tint[3] = (uint8_t)fa;
        }
    }

    int16_t tone[4];
    int toned = ttm_tone_values(mrb_iv_get(mrb, self, IV("@tone")), tone);
    if (toned) gfx_quad_tone(tone);

    double px = vx + s->x - vox, py = vy + s->y - voy;
    double rad = s->angle * 3.14159265358979 / 180.0;
    double ca = cos(rad), sa = sin(rad);
    /* rows [v0, v1) of the source rect, drawn with alpha */
    int bush = s->bush_depth > 0 ? (s->bush_depth < sh ? s->bush_depth : sh) : 0;
    int parts[2][3] = { { 0, sh - bush, s->opacity }, { sh - bush, sh, s->opacity / 2 } };
    for (int k = 0; k < 2; ++k) {
        int v0 = parts[k][0], v1 = parts[k][1];
        if (v1 <= v0) continue;
        /* top-left of rows v0..v1 in sprite space, zoomed, rotated about the
         * origin, placed; the backend rotates the rect about that corner */
        double lx = -ox * s->zoom_x, ly = (v0 - oy) * s->zoom_y;
        gfx_q q;
        q.sx = (float)sx; q.sy = (float)(sy + v0); q.sw = (float)sw; q.sh = (float)(v1 - v0);
        q.x = (float)(px + lx * ca + ly * sa);
        q.y = (float)(py - lx * sa + ly * ca);
        q.w = (float)(sw * s->zoom_x);
        q.h = (float)((v1 - v0) * s->zoom_y);
        q.angle = (float)rad;
        q.mirror = s->mirror;
        ttm_bitmap_draw(bm, q, parts[k][2], s->blend_type, tint);
    }
    if (toned) gfx_quad_tone(NULL);
}

void ttm_init_sprite(mrb_state* mrb)
{
    struct RClass* c = mrb_define_class(mrb, "Sprite", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_method(mrb, c, "initialize", sprite_init, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, c, "dispose", sprite_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", sprite_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "bitmap", sprite_bitmap, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "bitmap=", sprite_set_bitmap, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "flash", sprite_flash, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, c, "update", sprite_update, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "color", sprite_color, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "color=", sprite_set_color, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "tone", sprite_tone, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "tone=", sprite_set_tone, MRB_ARGS_REQ(1));
#define DEF(name)                                                                       \
    mrb_define_method(mrb, c, #name, sprite_get_##name, MRB_ARGS_NONE());               \
    mrb_define_method(mrb, c, #name "=", sprite_set_##name, MRB_ARGS_REQ(1));
    DEF(src_rect) DEF(viewport) DEF(x) DEF(y) DEF(z) DEF(ox) DEF(oy) DEF(zoom_x) DEF(zoom_y)
    DEF(angle) DEF(mirror) DEF(bush_depth) DEF(blend_type) DEF(visible) DEF(opacity)
#undef DEF
}
