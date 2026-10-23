/*
 * window.c -- RGSS1 Window, drawn from its windowskin (192x128):
 *
 *   (0,0,128,128)    background, stretched (or tiled) inside a 2-pixel margin,
 *                    at back_opacity * opacity
 *   (128,0,64,64)    frame: 16-pixel corners, edges tiled between them, at opacity
 *   (128,64,32,32)   cursor, stretched with 2-pixel borders over cursor_rect,
 *                    blinking while the window is active
 *   (160,64,32,32)   pause sign: four 16x16 frames, bottom centre
 *   arrows           (152,16) up, (152,40) down, (144,24) left, (168,24) right,
 *                    shown when the contents scroll beyond the window
 *
 * Contents are drawn 16 pixels in, clipped to the inside, scrolled by ox/oy, at
 * contents_opacity (which window opacity does not affect, as in RGSS1).
 */

#include <stdlib.h>

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/variable.h>

#include "gfx.h"

typedef struct {
    ttm_drawable d;
    int x, y, width, height, ox, oy;
    int opacity, back_opacity, contents_opacity;
    int stretch, active, pause;
    int cursor_count, pause_count;
} ttm_window;

static void window_free(mrb_state* mrb, void* p)
{
    ttm_window* w = (ttm_window*)p;
    if (!w) return;
    if (!w->d.disposed) ttm_unregister(&w->d);
    free(w);
}

static const mrb_data_type window_type = { "Window", window_free };

static ttm_window* get(mrb_state* mrb, mrb_value self)
{
    ttm_window* w = (ttm_window*)mrb_data_get_ptr(mrb, self, &window_type);
    if (!w) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "uninitialized window");
    return w;
}

#define IV(name) mrb_intern_lit(mrb, name)

static mrb_value window_init(mrb_state* mrb, mrb_value self)
{
    mrb_value viewport = mrb_nil_value();
    mrb_get_args(mrb, "|o", &viewport);
    ttm_window* w = (ttm_window*)calloc(1, sizeof(ttm_window));
    w->opacity = w->back_opacity = w->contents_opacity = 255;
    w->stretch = w->active = 1;
    mrb_data_init(self, w, &window_type);
    ttm_register(&w->d, TTM_WINDOW, (struct RData*)mrb_obj_ptr(self));
    mrb_iv_set(mrb, self, IV("@viewport"), viewport);
    mrb_iv_set(mrb, self, IV("@cursor_rect"), ttm_rect_new(mrb, 0, 0, 0, 0));
    return self;
}

static mrb_value window_dispose(mrb_state* mrb, mrb_value self)
{
    ttm_window* w = get(mrb, self);
    if (!w->d.disposed) ttm_unregister(&w->d);
    return mrb_nil_value();
}
static mrb_value window_disposed(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->d.disposed); }

static mrb_value window_update(mrb_state* mrb, mrb_value self)
{
    ttm_window* w = get(mrb, self);
    w->cursor_count = (w->cursor_count + 1) % 32;
    w->pause_count = (w->pause_count + 1) % 64;
    return mrb_nil_value();
}

#define OBJ_PROP(name, iv)                                                          \
    static mrb_value window_get_##name(mrb_state* mrb, mrb_value self)             \
    { return mrb_iv_get(mrb, self, IV(iv)); }                                        \
    static mrb_value window_set_##name(mrb_state* mrb, mrb_value self)             \
    { mrb_value v; mrb_get_args(mrb, "o", &v); mrb_iv_set(mrb, self, IV(iv), v); return v; }
OBJ_PROP(windowskin, "@windowskin")
OBJ_PROP(contents, "@contents")
OBJ_PROP(cursor_rect, "@cursor_rect")
OBJ_PROP(viewport, "@viewport")

static int clamp255(mrb_int v) { return v < 0 ? 0 : v > 255 ? 255 : (int)v; }

#define NUM_PROP(name, get_expr, set_stmt)                                              \
    static mrb_value window_get_##name(mrb_state* mrb, mrb_value self)                 \
    { ttm_window* w = get(mrb, self); return get_expr; }                                 \
    static mrb_value window_set_##name(mrb_state* mrb, mrb_value self)                 \
    { mrb_value v; mrb_get_args(mrb, "o", &v); ttm_window* w = get(mrb, self); set_stmt; return v; }

NUM_PROP(x, mrb_int_value(mrb, w->x), w->x = (int)ttm_to_int(mrb, v))
NUM_PROP(y, mrb_int_value(mrb, w->y), w->y = (int)ttm_to_int(mrb, v))
NUM_PROP(z, mrb_int_value(mrb, w->d.z), ttm_set_z(&w->d, (int)ttm_to_int(mrb, v)))
NUM_PROP(width, mrb_int_value(mrb, w->width), w->width = (int)ttm_to_int(mrb, v))
NUM_PROP(height, mrb_int_value(mrb, w->height), w->height = (int)ttm_to_int(mrb, v))
NUM_PROP(ox, mrb_int_value(mrb, w->ox), w->ox = (int)ttm_to_int(mrb, v))
NUM_PROP(oy, mrb_int_value(mrb, w->oy), w->oy = (int)ttm_to_int(mrb, v))
NUM_PROP(opacity, mrb_int_value(mrb, w->opacity), w->opacity = clamp255(ttm_to_int(mrb, v)))
NUM_PROP(back_opacity, mrb_int_value(mrb, w->back_opacity), w->back_opacity = clamp255(ttm_to_int(mrb, v)))
NUM_PROP(contents_opacity, mrb_int_value(mrb, w->contents_opacity),
         w->contents_opacity = clamp255(ttm_to_int(mrb, v)))
NUM_PROP(stretch, mrb_bool_value(w->stretch), w->stretch = mrb_test(v))
NUM_PROP(active, mrb_bool_value(w->active), w->active = mrb_test(v))
NUM_PROP(pause, mrb_bool_value(w->pause), w->pause = mrb_test(v))
NUM_PROP(visible, mrb_bool_value(w->d.visible), w->d.visible = mrb_test(v))

/* ---- drawing --------------------------------------------------------------------------- */

static const uint8_t no_tint[4] = { 0, 0, 0, 0 };

/* skin rect (sx,sy,sw,sh) stretched onto screen rect (dx,dy,dw,dh) */
static void blit(ttm_bitmap* t, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int alpha)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || alpha <= 0) return;
    gfx_q q = { (float)sx, (float)sy, (float)sw, (float)sh, (float)dx, (float)dy, (float)dw, (float)dh, 0, 0 };
    ttm_bitmap_draw(t, q, alpha, 0, no_tint);
}

/* skin rect tiled over screen rect (partial tiles at the right and bottom) */
static void tile(ttm_bitmap* t, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int alpha)
{
    for (int y = 0; y < dh; y += sh) {
        int h = dh - y < sh ? dh - y : sh;
        for (int x = 0; x < dw; x += sw) {
            int w = dw - x < sw ? dw - x : sw;
            blit(t, sx, sy, w, h, dx + x, dy + y, w, h, alpha);
        }
    }
}

void ttm_window_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int vox, int voy)
{
    ttm_window* w = (ttm_window*)d;
    if (!d->visible || w->width <= 0 || w->height <= 0) return;
    mrb_value self = mrb_obj_value(d->self);
    int x = vx + w->x - vox, y = vy + w->y - voy, W = w->width, H = w->height;

    ttm_bitmap* skin = ttm_bitmap_ptr(mrb_iv_get(mrb, self, IV("@windowskin")));
    ttm_bitmap* st = skin;

    if (st) {
        /* background */
        int ba = w->back_opacity * w->opacity / 255;
        if (w->stretch) blit(st, 0, 0, 128, 128, x + 2, y + 2, W - 4, H - 4, ba);
        else tile(st, 0, 0, 128, 128, x + 2, y + 2, W - 4, H - 4, ba);
        /* frame */
        int fa = w->opacity;
        blit(st, 128, 0, 16, 16, x, y, 16, 16, fa);
        blit(st, 176, 0, 16, 16, x + W - 16, y, 16, 16, fa);
        blit(st, 128, 48, 16, 16, x, y + H - 16, 16, 16, fa);
        blit(st, 176, 48, 16, 16, x + W - 16, y + H - 16, 16, 16, fa);
        tile(st, 144, 0, 32, 16, x + 16, y, W - 32, 16, fa);
        tile(st, 144, 48, 32, 16, x + 16, y + H - 16, W - 32, 16, fa);
        tile(st, 128, 16, 16, 32, x, y + 16, 16, H - 32, fa);
        tile(st, 176, 16, 16, 32, x + W - 16, y + 16, 16, H - 32, fa);
    }

    int cx = x + 16, cy = y + 16, cw = W - 32, ch = H - 32;

    /* cursor, under the contents, clipped to the inside */
    ttm_rect_t* cr = ttm_rect_ptr(mrb_iv_get(mrb, self, IV("@cursor_rect")));
    if (st && cr && cr->w > 0 && cr->h > 0) {
        int blink = w->cursor_count < 16 ? w->cursor_count : 32 - w->cursor_count;
        int ca = w->active ? 255 - blink * 6 : 128;
        int rx = cx + (int)cr->x - w->ox, ry = cy + (int)cr->y - w->oy;
        int rw = (int)cr->w, rh = (int)cr->h;
        ttm_clip_narrow(cx, cy, cw, ch);
        blit(st, 128, 64, 2, 2, rx, ry, 2, 2, ca);
        blit(st, 158, 64, 2, 2, rx + rw - 2, ry, 2, 2, ca);
        blit(st, 128, 94, 2, 2, rx, ry + rh - 2, 2, 2, ca);
        blit(st, 158, 94, 2, 2, rx + rw - 2, ry + rh - 2, 2, 2, ca);
        blit(st, 130, 64, 28, 2, rx + 2, ry, rw - 4, 2, ca);
        blit(st, 130, 94, 28, 2, rx + 2, ry + rh - 2, rw - 4, 2, ca);
        blit(st, 128, 66, 2, 28, rx, ry + 2, 2, rh - 4, ca);
        blit(st, 158, 66, 2, 28, rx + rw - 2, ry + 2, 2, rh - 4, ca);
        blit(st, 130, 66, 28, 28, rx + 2, ry + 2, rw - 4, rh - 4, ca);
        ttm_clip_restore();
    }

    /* contents */
    ttm_bitmap* contents = ttm_bitmap_ptr(mrb_iv_get(mrb, self, IV("@contents")));
    if (contents && w->contents_opacity > 0) {
        ttm_clip_narrow(cx, cy, cw, ch);
        blit(contents, 0, 0, contents->w, contents->h,
             cx - w->ox, cy - w->oy, contents->w, contents->h, w->contents_opacity);
        ttm_clip_restore();
    }

    if (st) {
        /* scroll arrows */
        if (contents) {
            if (w->ox > 0) blit(st, 144, 24, 8, 16, x + 4, y + H / 2 - 8, 8, 16, 255);
            if (w->oy > 0) blit(st, 152, 16, 16, 8, x + W / 2 - 8, y + 4, 16, 8, 255);
            if (contents->w - w->ox > cw) blit(st, 168, 24, 8, 16, x + W - 12, y + H / 2 - 8, 8, 16, 255);
            if (contents->h - w->oy > ch) blit(st, 152, 40, 16, 8, x + W / 2 - 8, y + H - 12, 16, 8, 255);
        }
        /* pause sign: four frames, 16 ticks each */
        if (w->pause) {
            int f = w->pause_count / 16;
            blit(st, 160 + (f & 1) * 16, 64 + (f >> 1) * 16, 16, 16, x + W / 2 - 8, y + H - 16, 16, 16, 255);
        }
    }
}

void ttm_init_window(mrb_state* mrb)
{
    struct RClass* c = mrb_define_class(mrb, "Window", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_method(mrb, c, "initialize", window_init, MRB_ARGS_OPT(1));
    mrb_define_method(mrb, c, "dispose", window_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", window_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "update", window_update, MRB_ARGS_NONE());
#define DEF(name)                                                                       \
    mrb_define_method(mrb, c, #name, window_get_##name, MRB_ARGS_NONE());               \
    mrb_define_method(mrb, c, #name "=", window_set_##name, MRB_ARGS_REQ(1));
    DEF(windowskin) DEF(contents) DEF(cursor_rect) DEF(viewport) DEF(x) DEF(y) DEF(z)
    DEF(width) DEF(height) DEF(ox) DEF(oy) DEF(opacity) DEF(back_opacity)
    DEF(contents_opacity) DEF(stretch) DEF(active) DEF(pause) DEF(visible)
#undef DEF
}
