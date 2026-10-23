/*
 * backend_soft.c -- the PC backend: a software rasterizer into a 640x480 frame.
 *
 * Not fast and not meant to be: it exists so the engine can run the game on the
 * PC and save frames as PNG (Graphics.ttm_screenshot), to check layout without a
 * console. Each quad is drawn by mapping every screen pixel inside it back to
 * the texture (nearest texel), then blending (normal, add, subtract).
 * Tone, viewport colour and flash, freeze and transition are done per pixel
 * with RGSS's own formulas, so the PC is the reference the console's GPU
 * version is compared with. The 3DS uses backend_c3d.c instead.
 */

#ifndef __3DS__

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "stb_image_write.h"

struct gfx_tex {
    int w, h;
    uint32_t* px;       /* NULL: over the pretend budget (gfx_tex_budget) */
    long bytes;
};

/* the 3DS's texture memory, pretended: what a texture would cost there
 * (both sides rounded up to a power of two, at least 8) against a budget */
static long budget_bytes, used_bytes;

void gfx_tex_budget(long kb) { budget_bytes = kb * 1024; }

static long pot(int v)
{
    long p = 8;
    while (p < v) p <<= 1;
    return p;
}

gfx_tex* gfx_tex_new(int w, int h)
{
    gfx_tex* t = (gfx_tex*)calloc(1, sizeof(gfx_tex));
    t->w = w;
    t->h = h;
    long bytes = pot(w) * pot(h) * 4;
    if (budget_bytes > 0 && used_bytes + bytes > budget_bytes) return t;
    t->px = (uint32_t*)calloc((size_t)w * h, 4);
    if (t->px) {
        t->bytes = bytes;
        used_bytes += bytes;
    }
    return t;
}

int gfx_tex_ok(const gfx_tex* t) { return t && t->px; }
long gfx_tex_bytes(const gfx_tex* t) { return t ? t->bytes : 0; }

void gfx_tex_upload(gfx_tex* t, const ttm_bitmap* b, int x0, int y0)
{
    if (!t->px) return;
    for (int y = 0; y < t->h; ++y)
        memcpy(t->px + (size_t)y * t->w, ttm_row(b, y0 + y) + x0, (size_t)t->w * 4);
}

void gfx_tex_free(gfx_tex* t)
{
    if (!t) return;
    used_bytes -= t->bytes;
    free(t->px);
    free(t);
}

/* The software rasterizer indexes any size, so a bitmap is one texture here
 * unless $ttm_max_tex_dim asks for slices (that is how the PC checks them). */
int gfx_max_tex_dim(void) { return 0; }

long gfx_mem_free_kb(void) { return 0; }
long gfx_mem_largest_kb(void) { return 0; }

/* the PC draws every frame, unpaced, so runs are the same every time */
int gfx_should_skip(void) { return 0; }
void gfx_frame_skipped(void) {}
void gfx_frame_reset(void) {}
long gfx_tex_mem_free_kb(void)
{
    return budget_bytes > 0 ? (budget_bytes - used_bytes) / 1024 : 0;
}

static uint32_t fb[TTM_SCREEN_W * TTM_SCREEN_H];
static int cx0, cy0, cx1, cy1;
/* where quads and fills go: the game's frame, or the bottom screen's
 * (gfx_bottom_begin .. gfx_bottom_end) */
static uint32_t bottom_fb[TTM_BOTTOM_W * TTM_BOTTOM_H];
static uint32_t* tgt = fb;
static int tw = TTM_SCREEN_W, th = TTM_SCREEN_H;

/* ---- tone ------------------------------------------------------------------------- */

static int16_t quad_tone[4];
static int quad_toned;

void gfx_quad_tone(const int16_t* tone)
{
    quad_toned = tone != NULL;
    if (tone) memcpy(quad_tone, tone, sizeof(quad_tone));
}

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* RGSS's tone: toward the luma by gray/255, then r, g, b added */
static void tone_px(int* r, int* g, int* b, const int16_t t[4])
{
    if (t[3]) {
        int l = (*r * 299 + *g * 587 + *b * 114 + 500) / 1000;
        *r += (l - *r) * t[3] / 255;
        *g += (l - *g) * t[3] / 255;
        *b += (l - *b) * t[3] / 255;
    }
    *r = clamp255(*r + t[0]);
    *g = clamp255(*g + t[1]);
    *b = clamp255(*b + t[2]);
}

#define PX(r, g, b) (0xff000000u | (uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

void gfx_region_tone(int x, int y, int w, int h, const int16_t tone[4])
{
    for (int py = y < 0 ? 0 : y; py < y + h && py < TTM_SCREEN_H; ++py)
        for (int px = x < 0 ? 0 : x; px < x + w && px < TTM_SCREEN_W; ++px) {
            uint32_t* d = &fb[py * TTM_SCREEN_W + px];
            int r = *d & 0xff, g = (*d >> 8) & 0xff, b = (*d >> 16) & 0xff;
            tone_px(&r, &g, &b, tone);
            *d = PX(r, g, b);
        }
}

void gfx_region_fill(int x, int y, int w, int h, const uint8_t c[4])
{
    int a = c[3];
    for (int py = y < 0 ? 0 : y; py < y + h && py < th; ++py)
        for (int px = x < 0 ? 0 : x; px < x + w && px < tw; ++px) {
            uint32_t* d = &tgt[py * tw + px];
            int r = *d & 0xff, g = (*d >> 8) & 0xff, b = (*d >> 16) & 0xff;
            *d = PX((c[0] * a + r * (255 - a)) / 255, (c[1] * a + g * (255 - a)) / 255,
                    (c[2] * a + b * (255 - a)) / 255);
        }
}

/* ---- freeze and transition ---------------------------------------------------------- */

static uint32_t frozen_fb[TTM_SCREEN_W * TTM_SCREEN_H];
static uint8_t* trans_mask;

/* fb still holds the frame last drawn: gfx_frame_begin clears it only when
 * the next one starts */
void gfx_freeze(void) { memcpy(frozen_fb, fb, sizeof(fb)); }

void gfx_transition_mask(const uint8_t* mask)
{
    free(trans_mask);
    trans_mask = NULL;
    if (mask && (trans_mask = (uint8_t*)malloc((size_t)TTM_SCREEN_W * TTM_SCREEN_H)))
        memcpy(trans_mask, mask, (size_t)TTM_SCREEN_W * TTM_SCREEN_H);
}

void gfx_frozen_draw(int alpha, const uint8_t* lut)
{
    for (int i = 0; i < TTM_SCREEN_W * TTM_SCREEN_H; ++i) {
        int a = lut && trans_mask ? lut[trans_mask[i]] : alpha;
        if (a <= 0) continue;
        uint32_t o = frozen_fb[i], n = fb[i];
        int r = ((int)(o & 0xff) * a + (int)(n & 0xff) * (255 - a)) / 255;
        int g = ((int)((o >> 8) & 0xff) * a + (int)((n >> 8) & 0xff) * (255 - a)) / 255;
        int b = ((int)((o >> 16) & 0xff) * a + (int)((n >> 16) & 0xff) * (255 - a)) / 255;
        fb[i] = PX(r, g, b);
    }
}

void gfx_frame_begin(void)
{
    for (int i = 0; i < TTM_SCREEN_W * TTM_SCREEN_H; ++i) fb[i] = 0xff000000u;
    tgt = fb;
    tw = TTM_SCREEN_W;
    th = TTM_SCREEN_H;
    cx0 = cy0 = 0;
    cx1 = TTM_SCREEN_W;
    cy1 = TTM_SCREEN_H;
}

void gfx_clip(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x;
    cy0 = y < 0 ? 0 : y;
    cx1 = x + w > tw ? tw : x + w;
    cy1 = y + h > th ? th : y + h;
}

/* the bottom screen: its own buffer, cleared to the 3DS backend's colour */
int gfx_bottom_begin(void)
{
    for (int i = 0; i < TTM_BOTTOM_W * TTM_BOTTOM_H; ++i) bottom_fb[i] = PX(18, 20, 34);
    tgt = bottom_fb;
    tw = TTM_BOTTOM_W;
    th = TTM_BOTTOM_H;
    cx0 = cy0 = 0;
    cx1 = tw;
    cy1 = th;
    return 1;
}

void gfx_bottom_end(int dim)
{
    if (dim > 0) {
        const uint8_t veil[4] = { 0, 0, 0, (uint8_t)dim };
        gfx_region_fill(0, 0, tw, th, veil);
    }
    tgt = fb;
    tw = TTM_SCREEN_W;
    th = TTM_SCREEN_H;
}

void gfx_quad(gfx_tex* t, const gfx_q* q, int alpha, int blend, const uint8_t tint[4])
{
    if (!t || !t->px || q->sw <= 0 || q->sh <= 0 || alpha <= 0 || cx1 <= cx0 || cy1 <= cy0) return;
    /* corners: top-left, top-right, bottom-left (rotated CCW about top-left) */
    float ca = cosf(q->angle), sa = sinf(q->angle);
    struct { float x, y; } dst[3] = {
        { q->x, q->y },
        { q->x + q->w * ca, q->y - q->w * sa },
        { q->x + q->h * sa, q->y + q->h * ca } };
    if (q->mirror) {                     /* same place, texture flipped */
        float tlx = dst[0].x, tly = dst[0].y;
        dst[0] = dst[1];
        dst[1].x = tlx; dst[1].y = tly;
        dst[2].x = dst[2].x + (dst[0].x - tlx);
        dst[2].y = dst[2].y + (dst[0].y - tly);
    }
    float sx = q->sx, sy = q->sy, sw = q->sw, sh = q->sh;
    /* per-texel axes of the parallelogram */
    float ex = (dst[1].x - dst[0].x) / sw, ey_ = (dst[1].y - dst[0].y) / sw;
    float fx = (dst[2].x - dst[0].x) / sh, fy = (dst[2].y - dst[0].y) / sh;
    float det = ex * fy - ey_ * fx;
    if (fabsf(det) < 1e-9f) return;
    float bx3 = dst[1].x + dst[2].x - dst[0].x, by3 = dst[1].y + dst[2].y - dst[0].y;
    float minx = fminf(fminf(dst[0].x, dst[1].x), fminf(dst[2].x, bx3));
    float maxx = fmaxf(fmaxf(dst[0].x, dst[1].x), fmaxf(dst[2].x, bx3));
    float miny = fminf(fminf(dst[0].y, dst[1].y), fminf(dst[2].y, by3));
    float maxy = fmaxf(fmaxf(dst[0].y, dst[1].y), fmaxf(dst[2].y, by3));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    int ta = tint ? tint[3] : 0;
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float qx = px + 0.5f - dst[0].x, qy = py + 0.5f - dst[0].y;
            float u = (qx * fy - qy * fx) / det;
            float v = (ex * qy - ey_ * qx) / det;
            if (u < 0 || v < 0 || u >= sw || v >= sh) continue;
            int tx = (int)(sx + u), ty = (int)(sy + v);
            if (tx < 0 || ty < 0 || tx >= t->w || ty >= t->h) continue;
            uint32_t s = t->px[(size_t)ty * t->w + tx];
            int a = (int)(s >> 24) * alpha / 255;
            if (!a) continue;
            int r = s & 0xff, g = (s >> 8) & 0xff, b = (s >> 16) & 0xff;
            if (quad_toned) tone_px(&r, &g, &b, quad_tone);
            if (ta) {
                r += (tint[0] - r) * ta / 255;
                g += (tint[1] - g) * ta / 255;
                b += (tint[2] - b) * ta / 255;
            }
            uint32_t* d = &tgt[py * tw + px];
            int dr = *d & 0xff, dg = (*d >> 8) & 0xff, db = (*d >> 16) & 0xff;
            if (blend == 1) {
                dr += r * a / 255; dg += g * a / 255; db += b * a / 255;
                if (dr > 255) dr = 255;
                if (dg > 255) dg = 255;
                if (db > 255) db = 255;
            } else if (blend == 2) {
                dr -= r * a / 255; dg -= g * a / 255; db -= b * a / 255;
                if (dr < 0) dr = 0;
                if (dg < 0) dg = 0;
                if (db < 0) db = 0;
            } else {
                dr = (r * a + dr * (255 - a)) / 255;
                dg = (g * a + dg * (255 - a)) / 255;
                db = (b * a + db * (255 - a)) / 255;
            }
            *d = 0xff000000u | (uint32_t)dr | ((uint32_t)dg << 8) | ((uint32_t)db << 16);
        }
    }
}

void gfx_frame_end(void) {}

const uint32_t* gfx_frame_pixels(void) { return fb; }

int gfx_running(void) { return 1; }

int gfx_save_png(const char* path)
{
    return stbi_write_png(path, TTM_SCREEN_W, TTM_SCREEN_H, 4, fb, TTM_SCREEN_W * 4) != 0;
}

int gfx_save_png_bottom(const char* path)
{
    return stbi_write_png(path, TTM_BOTTOM_W, TTM_BOTTOM_H, 4, bottom_fb, TTM_BOTTOM_W * 4) != 0;
}

#endif /* !__3DS__ */
