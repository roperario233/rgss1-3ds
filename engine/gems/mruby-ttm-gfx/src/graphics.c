/*
 * graphics.c -- the drawable registry, RGSS z order, and the Graphics module.
 *
 * Every live Viewport, Sprite and Window is registered here (and removed when
 * disposed or garbage-collected, as in RGSS). Graphics.update draws them:
 * viewports and viewport-less objects share the screen's z order; each
 * viewport's own objects are drawn in its slot, sorted by z, clipped to it and
 * scrolled by its ox/oy. Equal z: the object created, or given that z, later
 * is on top (mkxp's rule; the game's message system relies on it).
 *
 * A viewport's tone, colour and flash are drawn over its rect once its
 * contents are. Graphics.freeze keeps the frame last shown; until
 * Graphics.transition the screen shows that frame while the game builds the
 * next scene, and transition fades from it to the new scene -- evenly, or
 * pixel by pixel in the order a transition image gives (Graphics/Transitions).
 *
 * After drawing, Graphics.update calls $ttm_frame_hook if it is set (the test
 * harness times frames with it).
 *
 * The bottom screen: what is made inside Graphics.ttm_on_bottom (the game's
 * menu, romfs/rgss/ttm_patches.rb) is drawn in a second pass of its own, z
 * order and viewports as above, with game space mapped onto the 320x240
 * screen by the bottom view (Graphics.ttm_bottom_view). Freeze and transition
 * are the top screen's only; tone, colour and flash of a bottom viewport are
 * not drawn (the menu has none).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "gfx.h"

#if defined(__3DS__)
#include <3ds.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

int gfx_save_png(const char* path);   /* backend_soft.c; 0 on the 3DS */

/* ---- helpers ------------------------------------------------------------------------ */

mrb_int ttm_to_int(mrb_state* mrb, mrb_value v)
{
    if (mrb_integer_p(v)) return mrb_integer(v);
    if (mrb_float_p(v)) return (mrb_int)mrb_float(v);
    return mrb_as_int(mrb, v);
}

double ttm_to_dbl(mrb_state* mrb, mrb_value v)
{
    if (mrb_float_p(v)) return mrb_float(v);
    if (mrb_integer_p(v)) return (double)mrb_integer(v);
    return mrb_as_float(mrb, v);
}

mrb_value ttm_iv(mrb_state* mrb, struct RData* self, const char* name)
{
    return mrb_iv_get(mrb, mrb_obj_value(self), mrb_intern_cstr(mrb, name));
}

mrb_value ttm_drawable_viewport(mrb_state* mrb, ttm_drawable* d)
{
    return mrb_iv_get(mrb, mrb_obj_value(d->self), mrb_intern_lit(mrb, "@viewport"));
}

static uint64_t now_us(void)
{
#if defined(__3DS__)
    return svcGetSystemTick() * 1000000ULL / SYSCLOCK_ARM11;
#elif defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / (f.QuadPart / 1000000));
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
#endif
}

/* ---- registry -------------------------------------------------------------------------- */

static ttm_drawable** items;
static size_t n_items, cap_items;
static uint32_t next_id;
static int screen_now;    /* what is made now belongs to this screen (Graphics.ttm_screen=) */

void ttm_register(ttm_drawable* d, int kind, struct RData* self)
{
    d->kind = kind;
    d->self = self;
    d->id = ++next_id;
    d->z = 0;
    d->visible = 1;
    d->disposed = 0;
    d->screen = screen_now;
    if (n_items == cap_items) {
        cap_items = cap_items ? cap_items * 2 : 256;
        items = (ttm_drawable**)realloc(items, cap_items * sizeof(*items));
    }
    items[n_items++] = d;
}

void ttm_set_z(ttm_drawable* d, int z)
{
    if (d->z == z) return;
    d->z = z;
    d->id = ++next_id;
}

void ttm_unregister(ttm_drawable* d)
{
    d->disposed = 1;
    for (size_t i = 0; i < n_items; ++i) {
        if (items[i] == d) {
            items[i] = items[--n_items];
            return;
        }
    }
}

/* ---- clipping ------------------------------------------------------------------------------ */

static int base_x0, base_y0, base_x1, base_y1;     /* the viewport (or screen) */
static int clip_x0, clip_y0, clip_x1, clip_y1;     /* current: base, maybe narrowed */

/* the bottom pass: game space -> bottom screen, x' = x * bv_s + bv_dx */
static int bottom_pass;
static float bv_s = 0.5f, bv_dx, bv_dy;

static void apply_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1;
    if (bottom_pass) {
        int sx0 = (int)(x0 * bv_s + bv_dx), sy0 = (int)(y0 * bv_s + bv_dy);
        gfx_clip(sx0, sy0, (int)(x1 * bv_s + bv_dx + 0.999f) - sx0, (int)(y1 * bv_s + bv_dy + 0.999f) - sy0);
    } else {
        gfx_clip(x0, y0, x1 - x0, y1 - y0);
    }
}

static void set_base(int x0, int y0, int x1, int y1)
{
    base_x0 = x0; base_y0 = y0; base_x1 = x1; base_y1 = y1;
    apply_clip(x0, y0, x1, y1);
}

void ttm_clip_narrow(int x, int y, int w, int h)
{
    int x0 = x > base_x0 ? x : base_x0, y0 = y > base_y0 ? y : base_y0;
    int x1 = x + w < base_x1 ? x + w : base_x1, y1 = y + h < base_y1 ? y + h : base_y1;
    apply_clip(x0, y0, x1, y1);
}

void ttm_clip_current(int* x0, int* y0, int* x1, int* y1)
{
    *x0 = clip_x0; *y0 = clip_y0; *x1 = clip_x1; *y1 = clip_y1;
}

void ttm_clip_restore(void)
{
    apply_clip(base_x0, base_y0, base_x1, base_y1);
}

void ttm_draw(gfx_tex* t, gfx_q q, int alpha, int blend, const uint8_t tint[4])
{
    if (!t || alpha <= 0 || q.w == 0 || q.h == 0 || q.sw <= 0 || q.sh <= 0) return;
    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;
    if (q.angle == 0 && q.w > 0 && q.h > 0) {
        float kx = q.sw / q.w, ky = q.sh / q.h;          /* texels per screen pixel */
        float cut;
        if ((cut = clip_x0 - q.x) > 0) {                 /* left */
            if (cut >= q.w) return;
            if (!q.mirror) q.sx += cut * kx;
            q.sw -= cut * kx; q.w -= cut; q.x = (float)clip_x0;
        }
        if ((cut = q.x + q.w - clip_x1) > 0) {           /* right */
            if (cut >= q.w) return;
            if (q.mirror) q.sx += cut * kx;
            q.sw -= cut * kx; q.w -= cut;
        }
        if ((cut = clip_y0 - q.y) > 0) {                 /* top */
            if (cut >= q.h) return;
            q.sy += cut * ky; q.sh -= cut * ky; q.h -= cut; q.y = (float)clip_y0;
        }
        if ((cut = q.y + q.h - clip_y1) > 0) {           /* bottom */
            if (cut >= q.h) return;
            q.sh -= cut * ky; q.h -= cut;
        }
    }
    if (bottom_pass) {    /* clipped in game space; now onto the bottom screen */
        q.x = q.x * bv_s + bv_dx;
        q.y = q.y * bv_s + bv_dy;
        q.w *= bv_s;
        q.h *= bv_s;
    }
    gfx_quad(t, &q, alpha, blend, tint);
}

/* ---- tone and viewport effects -------------------------------------------------------------- */

int ttm_tone_values(mrb_value tone, int16_t out[4])
{
    ttm_quad_t* t = ttm_tone_ptr(tone);
    if (!t) return 0;
    for (int i = 0; i < 4; ++i) {
        double v = t->v[i];
        double lo = i < 3 ? -255 : 0;
        v = v < lo ? lo : v > 255 ? 255 : v;
        out[i] = (int16_t)(v < 0 ? v - 0.5 : v + 0.5);
    }
    return out[0] || out[1] || out[2] || out[3];
}

/* a viewport's tone, then its colour, then its flash, over its rect */
static void viewport_effects(mrb_state* mrb, ttm_viewport* vp, int x0, int y0, int x1, int y1)
{
    int16_t tone[4];
    if (ttm_tone_values(ttm_iv(mrb, vp->d.self, "@tone"), tone)) gfx_region_tone(x0, y0, x1 - x0, y1 - y0, tone);
    uint8_t c[4];
    ttm_color_bytes(ttm_iv(mrb, vp->d.self, "@color"), c);
    if (c[3]) gfx_region_fill(x0, y0, x1 - x0, y1 - y0, c);
    if (vp->flash_count > 0 && vp->flash_duration > 0) {
        uint8_t f[4] = { vp->flash[0], vp->flash[1], vp->flash[2],
                         (uint8_t)(vp->flash[3] * vp->flash_count / vp->flash_duration) };
        if (f[3]) gfx_region_fill(x0, y0, x1 - x0, y1 - y0, f);
    }
}

/* ---- rendering ---------------------------------------------------------------------------- */

static int by_z(const void* a, const void* b)
{
    const ttm_drawable* x = *(ttm_drawable* const*)a;
    const ttm_drawable* y = *(ttm_drawable* const*)b;
    if (x->z != y->z) return x->z < y->z ? -1 : 1;
    return x->id < y->id ? -1 : (x->id > y->id ? 1 : 0);
}

static void draw_one(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int ox, int oy)
{
    if (d->kind == TTM_SPRITE) ttm_sprite_draw(mrb, d, vx, vy, ox, oy);
    else if (d->kind == TTM_WINDOW) ttm_window_draw(mrb, d, vx, vy, ox, oy);
    else if (d->kind == TTM_TILEMAP_GROUND || d->kind == TTM_TILEMAP_ROW) ttm_tilemap_draw(mrb, d, vx, vy, ox, oy);
    else if (d->kind == TTM_PLANE) ttm_plane_draw(mrb, d, vx, vy, ox, oy);
}

uint64_t ttm_render_us_last;  /* the last frame's render time (also the 3DS fps line) */
uint32_t ttm_render_frame;

/* per-second figures for the 3DS's status line (backend_c3d.c resets them):
 * game updates, frames drawn, the game's own Ruby time between updates, and
 * the time spent drawing (up to handing the frame to the GPU) */
int ttm_stat_updates, ttm_stat_drawn;
uint64_t ttm_stat_ruby_us, ttm_stat_draw_us;

/* during Graphics.transition: the kept frame over each new frame, at this
 * opacity, or per pixel through lut and the transition image */
static int overlay_on, overlay_alpha;
static const uint8_t* overlay_lut;

static int bottom_dim;    /* Graphics.ttm_bottom_dim= : the veil over the bottom screen */

/* one screen's drawables: the viewport-less ones and the viewports in z order
 * (top[], sorted), each viewport's own objects in its slot */
static void draw_screen(mrb_state* mrb, int screen, ttm_drawable** top, size_t nt, ttm_drawable** kids,
                        struct RData** vp_of, size_t n)
{
    for (size_t t = 0; t < nt; ++t) {
        ttm_drawable* d = top[t];
        if (d->screen != screen) continue;
        if (d->kind != TTM_VIEWPORT) {
            set_base(0, 0, TTM_SCREEN_W, TTM_SCREEN_H);
            draw_one(mrb, d, 0, 0, 0, 0);
            continue;
        }
        ttm_viewport* vp = (ttm_viewport*)d;
        if (!d->visible) continue;
        ttm_rect_t* r = ttm_rect_ptr(ttm_iv(mrb, d->self, "@rect"));
        if (!r) continue;
        int x0 = r->x > 0 ? (int)r->x : 0, y0 = r->y > 0 ? (int)r->y : 0;
        int x1 = r->x + r->w < TTM_SCREEN_W ? (int)(r->x + r->w) : TTM_SCREEN_W;
        int y1 = r->y + r->h < TTM_SCREEN_H ? (int)(r->y + r->h) : TTM_SCREEN_H;
        if (x1 <= x0 || y1 <= y0) continue;
        size_t nk = 0;
        for (size_t i = 0; i < n; ++i)
            if (vp_of[i] == d->self) kids[nk++] = items[i];
        qsort(kids, nk, sizeof(*kids), by_z);
        set_base(x0, y0, x1, y1);
        for (size_t k = 0; k < nk; ++k) draw_one(mrb, kids[k], (int)r->x, (int)r->y, vp->ox, vp->oy);
        if (screen == 0) viewport_effects(mrb, vp, x0, y0, x1, y1);
    }
}

/* a frame: the top screen (or, while the scene is built under Graphics.freeze,
 * the frozen frame), then the bottom screen */
static void render(mrb_state* mrb, int frozen_top)
{
    uint64_t t0 = now_us();
    ttm_render_frame++;
    int ai = mrb_gc_arena_save(mrb);
    gfx_frame_begin();

    size_t n = n_items;
    ttm_drawable** top = (ttm_drawable**)malloc((n + 1) * sizeof(*top));
    ttm_drawable** kids = (ttm_drawable**)malloc((n + 1) * sizeof(*kids));
    struct RData** vp_of = (struct RData**)malloc((n + 1) * sizeof(*vp_of));
    size_t nt = 0;
    int bottom_items = 0;
    for (size_t i = 0; i < n; ++i) {
        ttm_drawable* d = items[i];
        vp_of[i] = NULL;
        if (d->kind == TTM_VIEWPORT) {
            top[nt++] = d;
            bottom_items |= d->screen;
            continue;
        }
        /* hidden: nothing to draw, so not even its viewport is looked up (the
         * game's weather keeps 500 hidden sprites on every map) */
        if (!d->visible) continue;
        mrb_value v = ttm_drawable_viewport(mrb, d);
        if (mrb_nil_p(v)) top[nt++] = d;
        else if (mrb_data_p(v)) vp_of[i] = (struct RData*)mrb_obj_ptr(v);
        bottom_items |= d->screen;
    }
    qsort(top, nt, sizeof(*top), by_z);

    if (frozen_top) gfx_frozen_draw(255, NULL);
    else draw_screen(mrb, 0, top, nt, kids, vp_of, n);
    if (overlay_on) gfx_frozen_draw(overlay_alpha, overlay_lut);    /* Graphics.transition */
    if (gfx_bottom_begin()) {
        if (bottom_items) {
            bottom_pass = 1;
            draw_screen(mrb, 1, top, nt, kids, vp_of, n);
            bottom_pass = 0;
        }
        gfx_bottom_end(bottom_dim);
    }
    free(top);
    free(kids);
    free(vp_of);

    ttm_stat_draw_us += now_us() - t0;
    ttm_stat_drawn++;
    gfx_frame_end();
    mrb_gc_arena_restore(mrb, ai);
    ttm_render_us_last = now_us() - t0;
}

/* ---- Graphics module ---------------------------------------------------------------------- */

static mrb_int frame_count, frame_rate = 40;

int ttm_frame_rate(void) { return (int)frame_rate; }

/* ---- memory the garbage collector cannot see ------------------------------------------
 * A Bitmap is a small object to mruby's collector but holds megabytes outside
 * it: pixels in RAM and textures in the 3DS's linear heap. The game drops
 * bitmaps without disposing them and leaves them to the collector, which runs
 * by object count -- long after that memory has run out (console run 4).
 * Texture memory mostly looks after itself: a dead bitmap is never drawn, so
 * bitmap.c's eviction takes its textures first. RAM it cannot free that way,
 * so between frames, when RAM is low or a texture could not be made even after
 * eviction, a full collection runs (at most once a second). Texture memory
 * being merely full is normal -- the tileset alone is 10 MB -- and is no reason
 * to collect. The RAM figure is 0 where the platform cannot say (the PC). */

static void log_line(mrb_state* mrb, const char* msg)
{
    mrb_value main = mrb_top_self(mrb);
    if (mrb_respond_to(mrb, main, mrb_intern_lit(mrb, "ttm_log")))
        mrb_funcall(mrb, main, "ttm_log", 1, mrb_str_new_cstr(mrb, msg));
}

static mrb_int last_gc = -1000;
static long gc_runs, failed_seen, failed_logged_at = -1000;

static void relieve_memory(mrb_state* mrb)
{
    mrb_value budget = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_tex_budget_kb"));
    if (mrb_integer_p(budget)) gfx_tex_budget((long)mrb_integer(budget));

    long count, bytes, failed;
    ttm_bitmap_tex_stats(&count, &bytes, &failed);
    long tex = gfx_tex_mem_free_kb(), ram = gfx_mem_free_kb();
    int low = ram > 0 && ram < 16 * 1024;
    int failing = failed > failed_seen;
    if (low) ttm_bitmap_unkeep_all();    /* decoded images kept after a dispose */
    ttm_bitmap_relieve(ram);    /* images' pixels, when RAM is under the reserve */
    if ((low || failing) && frame_count - last_gc >= frame_rate) {
        mrb_full_gc(mrb);
        last_gc = frame_count;
        gc_runs++;
    }
    if (failing && frame_count - failed_logged_at >= frame_rate * 5) {
        char msg[320], ramnote[128];
        ttm_bitmap_ram_note(ramnote, sizeof(ramnote));
        snprintf(msg, sizeof(msg), "gfx: texture memory full, %ld textures not made so far; "
                 "%ld bitmaps hold %ld KB, %ld KB free; RAM: %s", failed, count, bytes / 1024, tex, ramnote);
        log_line(mrb, msg);
        failed_logged_at = frame_count;
    }
    failed_seen = failed;
}

static uint64_t update_end_us;
static int skips_in_row;
/* the bottom screen's veil (see Graphics.ttm_bottom_dim= below) */
#define BOTTOM_DIM_OFF_MAP 150
static int dim_reported;

/* after each frame (Graphics.update, and each frame of a transition): a frame
 * Scene_Map did not report is one off the map, where the menu is veiled */
static void bottom_frame_end(void)
{
    if (!dim_reported) bottom_dim = BOTTOM_DIM_OFF_MAP;
    dim_reported = 0;
}

/* ---- freeze and transition ------------------------------------------------------------------
 * Every scene change in the game is Graphics.freeze, build the new scene,
 * Graphics.transition; map transfers keep updating the game for a few frames
 * in between. RGSS shows the frozen frame all that time, so a half-built
 * scene never appears. */

static int frozen;

/* the frozen frame on the top screen; the bottom screen goes on as usual */
static void show_frozen(mrb_state* mrb) { render(mrb, 1); }

static mrb_value g_freeze(mrb_state* mrb, mrb_value self)
{
    if (!frozen) {
        gfx_freeze();
        frozen = 1;
    }
    return mrb_nil_value();
}

/* a transition image as TTM_SCREEN_W x TTM_SCREEN_H values (its red channel,
 * stretched to the screen), or NULL when there is none to read */
static uint8_t* load_mask(mrb_state* mrb, mrb_value name)
{
    mrb_value ttm = mrb_obj_value(mrb_module_get(mrb, "TTM"));
    if (!mrb_string_p(mrb_funcall(mrb, ttm, "image_data", 1, name))) return NULL;
    mrb_value bmp = mrb_funcall(mrb, mrb_obj_value(mrb_class_get(mrb, "Bitmap")), "new", 1, name);
    ttm_bitmap* b = ttm_bitmap_ptr(bmp);
    uint8_t* mask = NULL;
    if (b && b->w > 0 && b->h > 0 && ttm_bitmap_pixels(b) && (mask = (uint8_t*)malloc((size_t)TTM_SCREEN_W * TTM_SCREEN_H))) {
        for (int y = 0; y < TTM_SCREEN_H; ++y) {
            const uint32_t* row = ttm_row(b, y * b->h / TTM_SCREEN_H);
            for (int x = 0; x < TTM_SCREEN_W; ++x)
                mask[(size_t)y * TTM_SCREEN_W + x] = (uint8_t)(row[x * b->w / TTM_SCREEN_W] & 0xff);
        }
    }
    if (b) mrb_funcall(mrb, bmp, "dispose", 0);
    return mask;
}

/* Graphics.transition(duration = 8, filename = "", vague = 40): from the
 * frozen frame to the scene as it is now, over `duration` frames. With a
 * transition image, a pixel turns over when the progress passes its value in
 * the image, blended across `vague` levels; else the whole frame fades. */
static mrb_value g_transition(mrb_state* mrb, mrb_value self)
{
    mrb_int duration = 8, vague = 40;
    mrb_value name = mrb_nil_value();
    mrb_get_args(mrb, "|ioi", &duration, &name, &vague);
    if (!frozen) return mrb_nil_value();
    uint8_t* mask = NULL;
    if (mrb_string_p(name) && RSTRING_LEN(name) > 0 && duration > 0) mask = load_mask(mrb, name);
    if (vague < 1) vague = 1;
    if (vague > 255) vague = 255;
    uint8_t lut[256];
    gfx_transition_mask(mask);
    for (mrb_int i = 0; i < duration && gfx_running(); ++i) {
        if (mask) {
            /* progress p runs from -vague to 255: a pixel of value m is the
             * old frame while m > p + vague, the new one once m <= p */
            int p = (int)(-vague + (255 + vague) * i / duration);
            for (int m = 0; m < 256; ++m) {
                int a = (m - p) * 255 / (int)vague;
                lut[m] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
            }
            overlay_lut = lut;
        } else {
            overlay_lut = NULL;
        }
        overlay_alpha = (int)(255 * (duration - i) / duration);
        overlay_on = 1;
        ttm_stat_updates++;
        if (skips_in_row < 2 && gfx_should_skip()) {
            gfx_frame_skipped();
            skips_in_row++;
        } else {
            render(mrb, 0);
            skips_in_row = 0;
        }
        overlay_on = 0;
        frame_count++;
        bottom_frame_end();
        mrb_value hook = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_frame_hook"));
        if (!mrb_nil_p(hook)) mrb_funcall(mrb, hook, "call", 0);
    }
    gfx_transition_mask(NULL);
    free(mask);
    frozen = 0;
    if (!gfx_running()) mrb_raise(mrb, mrb_class_get(mrb, "TTMQuit"), "the app is closing");
    return mrb_nil_value();
}

/* Graphics.update: one game frame. Drawn, unless it would be late and fewer
 * than two frames in a row were skipped already (gfx_should_skip) -- then the
 * game's update runs for it without drawing, as RGSS's frame skip does. */
static mrb_value g_update(mrb_state* mrb, mrb_value self)
{
    uint64_t t_in = now_us();
    if (update_end_us) ttm_stat_ruby_us += t_in - update_end_us;
    ttm_stat_updates++;
    relieve_memory(mrb);
    if (skips_in_row < 2 && gfx_should_skip()) {
        gfx_frame_skipped();
        skips_in_row++;
    } else if (frozen) {    /* the scene is being built under a frozen screen */
        show_frozen(mrb);
        skips_in_row = 0;
    } else {
        render(mrb, 0);
        skips_in_row = 0;
    }
    frame_count++;
    bottom_frame_end();
    if (!gfx_running())   /* the app is closing: unwind the game's loop */
        mrb_raise(mrb, mrb_class_get(mrb, "TTMQuit"), "the app is closing");
    mrb_value hook = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_frame_hook"));
    if (!mrb_nil_p(hook)) mrb_funcall(mrb, hook, "call", 0);
    update_end_us = now_us();
    return mrb_nil_value();
}

/* ---- the bottom screen (romfs/rgss/ttm_patches.rb, TTMBottomMenu) ----------------------- */

/* Graphics.ttm_screen = 0 / 1: the screen the drawables made from now on
 * belong to (rgss.rb's Graphics.ttm_on_bottom { ... } sets and restores it) */
static mrb_value g_set_screen(mrb_state* mrb, mrb_value self)
{
    mrb_int v;
    mrb_get_args(mrb, "i", &v);
    screen_now = v ? 1 : 0;
    return mrb_int_value(mrb, v);
}
static mrb_value g_screen(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, screen_now); }

/* Graphics.ttm_bottom_view(x, y, w, h): the part of game space the bottom
 * screen shows, scaled to fit and centred; returns [scale, dx, dy], screen =
 * game * scale + d */
static mrb_value g_bottom_view(mrb_state* mrb, mrb_value self)
{
    mrb_float x, y, w, h;
    mrb_get_args(mrb, "ffff", &x, &y, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    float sx = TTM_BOTTOM_W / (float)w, sy = TTM_BOTTOM_H / (float)h;
    bv_s = sx < sy ? sx : sy;
    bv_dx = (TTM_BOTTOM_W - (float)w * bv_s) / 2 - (float)x * bv_s;
    bv_dy = (TTM_BOTTOM_H - (float)h * bv_s) / 2 - (float)y * bv_s;
    mrb_value v[3] = { mrb_float_value(mrb, bv_s), mrb_float_value(mrb, bv_dx), mrb_float_value(mrb, bv_dy) };
    return mrb_ary_new_from_values(mrb, 3, v);
}

/* Graphics.ttm_bottom_dim = 0..255: the veil over the bottom screen for the
 * next frame. Scene_Map reports it each frame; a frame no one reports (any
 * other scene) is veiled (BOTTOM_DIM_OFF_MAP, declared above Graphics.update) */
static mrb_value g_set_bottom_dim(mrb_state* mrb, mrb_value self)
{
    mrb_int v;
    mrb_get_args(mrb, "i", &v);
    bottom_dim = v < 0 ? 0 : v > 255 ? 255 : (int)v;
    dim_reported = 1;
    return mrb_int_value(mrb, v);
}
static mrb_value g_bottom_dim(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, bottom_dim); }

/* Graphics.ttm_screenshot_bottom(path): the bottom screen as a PNG (PC build) */
static mrb_value g_screenshot_bottom(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    return mrb_bool_value(gfx_save_png_bottom(path));
}

static mrb_value g_frame_reset(mrb_state* mrb, mrb_value self)
{
    gfx_frame_reset();
    skips_in_row = 0;
    return mrb_nil_value();
}
static mrb_value g_frame_count(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, frame_count); }
static mrb_value g_set_frame_count(mrb_state* mrb, mrb_value self)
{
    mrb_int v;
    mrb_get_args(mrb, "i", &v);
    frame_count = v;
    return mrb_int_value(mrb, v);
}
static mrb_value g_frame_rate(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, frame_rate); }
static mrb_value g_set_frame_rate(mrb_state* mrb, mrb_value self)
{
    mrb_int v;
    mrb_get_args(mrb, "i", &v);
    frame_rate = v < 10 ? 10 : v > 120 ? 120 : v;
    return mrb_int_value(mrb, v);
}
static mrb_value g_width(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, TTM_SCREEN_W); }
static mrb_value g_height(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, TTM_SCREEN_H); }
static mrb_value g_bool_true(mrb_state* mrb, mrb_value self) { return mrb_true_value(); }
static mrb_value g_bool_set(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    return v;
}

static mrb_value g_snap(mrb_state* mrb, mrb_value self)
{
    mrb_value bmp = mrb_funcall(mrb, mrb_obj_value(mrb_class_get(mrb, "Bitmap")), "new", 2,
                                mrb_fixnum_value(TTM_SCREEN_W), mrb_fixnum_value(TTM_SCREEN_H));
    const uint32_t* fb = gfx_frame_pixels();
    ttm_bitmap* b = ttm_bitmap_ptr(bmp);
    if (fb && b) {
        for (int y = 0; y < TTM_SCREEN_H; ++y)
            memcpy(ttm_row(b, y), fb + (size_t)y * TTM_SCREEN_W, (size_t)TTM_SCREEN_W * 4);
        b->dirty = 1;
    }
    return bmp;
}

/* Graphics.ttm_screenshot(path): the last frame as a PNG (PC build) */
static mrb_value g_screenshot(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    return mrb_bool_value(gfx_save_png(path));
}

static mrb_value g_render_us(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, (mrb_int)ttm_render_us_last); }

/* [bitmaps holding textures, KB they hold, textures not made, forced collections] */
static mrb_value g_tex_stats(mrb_state* mrb, mrb_value self)
{
    long count, bytes, failed;
    ttm_bitmap_tex_stats(&count, &bytes, &failed);
    mrb_value a = mrb_ary_new(mrb);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, count));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, bytes / 1024));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, failed));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, gc_runs));
    return a;
}
/* Graphics.ttm_ram_stats: [KB of pixels in RAM, KB of them sheddable, images
 * shed, decoded again, reloads that failed] (pc/shed_check.rb) */
static mrb_value g_ram_stats(mrb_state* mrb, mrb_value self)
{
    long bytes, sheddable, shed, reloaded, failed;
    ttm_bitmap_ram_stats(&bytes, &sheddable, &shed, &reloaded, &failed);
    mrb_value a = mrb_ary_new(mrb);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, bytes / 1024));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, sheddable / 1024));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, shed));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, reloaded));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, failed));
    return a;
}
static mrb_value g_drawables(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, (mrb_int)n_items); }

void ttm_init_graphics(mrb_state* mrb)
{
    struct RClass* g = mrb_define_module(mrb, "Graphics");
    mrb_define_module_function(mrb, g, "update", g_update, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "freeze", g_freeze, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "transition", g_transition, MRB_ARGS_OPT(3));
    mrb_define_module_function(mrb, g, "frame_reset", g_frame_reset, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "frame_count", g_frame_count, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "frame_count=", g_set_frame_count, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "frame_rate", g_frame_rate, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "frame_rate=", g_set_frame_rate, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "width", g_width, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "height", g_height, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "snap_to_bitmap", g_snap, MRB_ARGS_NONE());
    /* mkxp extras the game sets; accepted and ignored */
    mrb_define_module_function(mrb, g, "fullscreen", g_bool_true, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "fullscreen=", g_bool_set, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "show_cursor", g_bool_true, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "show_cursor=", g_bool_set, MRB_ARGS_REQ(1));
    /* engine */
    mrb_define_module_function(mrb, g, "ttm_screenshot", g_screenshot, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "ttm_render_us", g_render_us, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_tex_stats", g_tex_stats, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_drawables", g_drawables, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_ram_stats", g_ram_stats, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_screen", g_screen, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_screen=", g_set_screen, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "ttm_bottom_view", g_bottom_view, MRB_ARGS_REQ(4));
    mrb_define_module_function(mrb, g, "ttm_bottom_dim", g_bottom_dim, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, g, "ttm_bottom_dim=", g_set_bottom_dim, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, g, "ttm_screenshot_bottom", g_screenshot_bottom, MRB_ARGS_REQ(1));
}
