/*
 * backend_c3d.c -- the 3DS backend, on citro2d / citro3d.
 *
 * The game draws on a 640x480 virtual screen: a render-target texture in VRAM
 * (1024x512). At the end of each frame that texture is drawn to the top screen,
 * bilinear: by default stretched corner to corner (400x240, so 5:3 -- the
 * picture is 25% wider than the game drew it), or, after SELECT, at half size
 * (320x240) centred with the game's own 4:3 shape -- layout option A from the
 * compatibility matrix (5.2).
 *
 * 3D (run 14): the top screen has a left and a right target and gfxSet3D() is
 * on. Each frame draws the frame texture to the left eye at x-d and to the
 * right at x+d, so the whole picture sits behind the screen by d (the slider
 * scales it). At slider 0 the two eyes are identical and the picture is flat.
 * Layered depth (text and menus in front of the map) is a later phase.
 *
 * The bottom screen, while the game runs, is graphics.c's second pass (the
 * game's own menu, romfs/rgss/ttm_patches.rb) drawn straight onto the screen's
 * render target, scaled down by the bottom view: no frame texture, so no VRAM
 * beyond the screen's own. Textures are sampled nearest everywhere else (the
 * game's pixels 1:1); in that pass each one is switched to linear until the
 * pass is flushed, so text scaled to 0.5 keeps its thin strokes. Before the
 * game starts, and on an error, the bottom screen is the app's text console.
 *
 * VRAM is two 3 MB banks (libctru's vramAlloc cannot cross them), so the screen
 * targets are made here without the depth buffers C2D_CreateScreenTarget asks
 * for: nothing here uses depth, and the three would cost 525 KB. That leaves
 * room for the 1 MB frame pair, the 256 KB frozen frame and both eyes.
 *
 * Textures: each Bitmap gets a power-of-two RGBA8 texture, filled on the CPU in
 * the PICA200's layout (8x8 Morton tiles, A,B,G,R bytes, image row 0 at v = 1;
 * see 3ds-app/source/main.c, M1). A bitmap over 1024 pixels on a side cannot
 * be a single texture: bitmap.c holds it as a grid of slices of at most
 * gfx_max_tex_dim() px, each one a texture here (a 256x9332 tileset is ten).
 *
 * Tone and transitions (no shaders here: the PICA200's six texture combiner
 * stages, set up between citro2d's own draws):
 *  - a viewport's tone (gray included) needs the pixels already drawn, so the
 *    frame ping-pongs between two render targets: the one drawn so far is
 *    drawn into the other through combiners that compute the luma
 *    (R*0.299 + G*0.587 + B*0.114 in three stages), move toward it by gray,
 *    then add and subtract the tone; drawing carries on in the other target;
 *  - a sprite's or plane's tone is the same combiners on its own quad;
 *  - Graphics.freeze copies the frame last shown into a third, smaller target
 *    (512x256 RGB565, 256 KB: the top screen shows at most 400x240); a
 *    transition draws it back over the new frame at 640x480, faded, or
 *    through a transition image: the image's per-pixel opacity is written
 *    into the frame's alpha channel first, and the frozen frame blended by
 *    destination alpha (which also puts that alpha back to opaque).
 * Each custom pass is queued with citro2d, given its combiners just before
 * citro2d flushes it, and followed by C2D_Prepare, which puts citro2d's own
 * state back.
 *
 * The app calls ttm_gfx3ds_init() after C3D_Init/C2D_Init, before Ruby starts.
 */

#ifdef __3DS__

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include <citro2d.h>
#include <c3d/renderqueue.h>

#include "gfx.h"

struct gfx_tex {
    C3D_Tex tex;
    int w, h, pw, ph;
    int ok;
    int linear;    /* switched to linear for the bottom pass (until gfx_bottom_end) */
};

/* the frame: drawn into frame[cur]; a viewport's tone moves it to the other */
static C3D_Tex frame_tex[2];
static C3D_RenderTarget* frame[2];
static int cur;
/* Graphics.freeze: the frame last shown, at reduced resolution (512x256) */
#define FROZEN_W 512
#define FROZEN_H 256
static C3D_Tex frozen_tex;
static C3D_RenderTarget* frozen_rt;
static int freeze_pending, frozen_ok;
/* a transition image: its values in the texture's tiled order, and the A8
 * texture that gets lut[value] each frame of the transition */
static C3D_Tex mask_tex;
static int mask_tex_ok, mask_on;
static uint8_t* mask_tiled;
/* the tone of the quads drawn next (gfx_quad_tone) */
static int16_t quad_tone_v[4];
static int quad_toned;

/* the top screen's two eyes (3D), the bottom screen: no depth buffers */
static C3D_RenderTarget* top;
static C3D_RenderTarget* top_r;
/* total parallax at full slider, in the top screen's 400x240 space */
#define TTM_DEPTH_PX 10
static int blend_now = -1;
static int too_big_logged;
/* the app's log hook (source/main.c sets it before ttm_gfx3ds_init) */
void (*ttm_gfx_log_fn)(const char* line);
/* the bottom pass (gfx_bottom_begin .. gfx_bottom_end) and the textures it
 * switched to linear */
#define MAX_LINEAR 512
static int in_bottom, n_linear;
static gfx_tex* linear_texs[MAX_LINEAR];

static int pot(int v)
{
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

static inline u32 tiled_index(u32 x, u32 y, u32 pw)
{
    u32 tile = (y >> 3) * (pw >> 3) + (x >> 3);
    u32 morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return (tile << 6) + morton;
}

gfx_tex* gfx_tex_new(int w, int h)
{
    gfx_tex* t = (gfx_tex*)calloc(1, sizeof(gfx_tex));
    t->w = w;
    t->h = h;
    if (w > 1024 || h > 1024) {    /* bitmap.c slices at gfx_max_tex_dim(): never */
        too_big_logged = 1;        /* (no printf: the bottom screen is not a console while the game runs) */
        return t;
    }
    t->pw = pot(w);
    t->ph = pot(h);
    if (!C3D_TexInit(&t->tex, (u16)t->pw, (u16)t->ph, GPU_RGBA8))
        return t;    /* out of linear memory: bitmap.c frees some and retries */
    memset(t->tex.data, 0, (size_t)t->pw * t->ph * 4);
    C3D_TexSetFilter(&t->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    t->ok = 1;
    return t;
}

void gfx_tex_upload(gfx_tex* t, const ttm_bitmap* b, int x0, int y0)
{
    if (!t->ok) return;
    u32* texels = (u32*)t->tex.data;
    int w = t->w, h = t->h;
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = ttm_row(b, y0 + y) + x0;
        for (int x = 0; x < w; ++x)
            texels[tiled_index((u32)x, (u32)y, (u32)t->pw)] = __builtin_bswap32(row[x]);
    }
    C3D_TexFlush(&t->tex);
}

int gfx_tex_ok(const gfx_tex* t) { return t && t->ok; }
long gfx_tex_bytes(const gfx_tex* t) { return t && t->ok ? (long)t->pw * t->ph * 4 : 0; }
void gfx_tex_budget(long kb) { (void)kb; }   /* the real limit applies */

static void bottom_forget(gfx_tex* t);

void gfx_tex_free(gfx_tex* t)
{
    if (t && t->linear) bottom_forget(t);
    if (!t) return;
    if (t->ok) C3D_TexDelete(&t->tex);
    free(t);
}

/* The PICA200 cannot sample a texture larger than 1024 px on a side, so
 * bitmap.c holds a larger bitmap as slices of at most this size. */
int gfx_max_tex_dim(void) { return 1024; }

/* textures live in the linear heap (C3D_TexInit -> linearAlloc) */
long gfx_tex_mem_free_kb(void) { return (long)(linearSpaceFree() / 1024); }

/* stb_image allocates through malloc, i.e. from the heap region. mallinfo's
 * fordblks alone is only the free space inside what malloc has already taken
 * from the heap (the first console run read "2635 KB free" with tens of MB
 * still untouched), so the headroom is that plus the heap not yet taken. */
long gfx_mem_free_kb(void)
{
    struct mallinfo mi = mallinfo();
    long untaken = (long)envGetHeapSize() - (long)mi.arena;
    if (untaken < 0) untaken = 0;
    return (untaken + (long)mi.fordblks) / 1024;
}

/* the largest block malloc gives now, to 16 KB: a binary search by allocating
 * (and freeing at once), so only for failure log lines */
long gfx_mem_largest_kb(void)
{
    size_t lo = 0, hi = envGetHeapSize();
    while (hi - lo > 16 * 1024) {
        size_t mid = lo + (hi - lo) / 2;
        void* p = malloc(mid);
        if (p) {
            free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (long)(lo / 1024);
}

/* colour: src-alpha blending (add, subtract); alpha channel kept opaque, so the
 * finished frame composites onto the screen without darkening */
static void set_blend(int blend)
{
    if (blend == blend_now) return;
    C2D_Flush();
    blend_now = blend;
    if (blend == 1)
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
    else if (blend == 2)
        C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
    else
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                       GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
}

/* top-screen layout: 1 = full screen (5:3, stretched), 0 = 4:3 at half size.
 * SELECT switches it (source/main.c). */
int ttm_top_stretch = 1;

/* a log line: log.txt through the app's hook (source/main.c), or the console */
static void gfx_log(const char* line)
{
    if (ttm_gfx_log_fn) ttm_gfx_log_fn(line);
    else printf("%s\n", line);
}

/* a render target over a VRAM texture: the frame pair is 1024x512, the frozen
 * frame 512x256 */
static C3D_RenderTarget* frame_target_sized(C3D_Tex* t, GPU_TEXCOLOR fmt, int w, int h)
{
    if (!C3D_TexInitVRAM(t, (u16)w, (u16)h, fmt)) return NULL;
    C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
    C3D_RenderTarget* rt = C3D_RenderTargetCreateFromTex(t, GPU_TEXFACE_2D, 0, -1);
    if (!rt) C3D_TexDelete(t);
    return rt;
}
static C3D_RenderTarget* frame_target(C3D_Tex* t, GPU_TEXCOLOR fmt)
{
    return frame_target_sized(t, fmt, 1024, 512);
}

/* A screen-sized target with no depth buffer, and the display-transfer flags
 * C2D_CreateScreenTarget uses (citro2d's base.c: RGBA8 in, RGB8 out, no
 * scaling). C3D_RenderTargetSetOutput must be called: citro2d reads the
 * target's linked flag as "tilted", and an unlinked 240x400 target would get
 * the wrong projection. */
static C3D_RenderTarget* screen_target(gfxScreen_t screen, gfx3dSide_t side, int h)
{
    C3D_RenderTarget* rt = C3D_RenderTargetCreate(GSP_SCREEN_WIDTH, h, GPU_RB_RGBA8, -1);
    if (!rt) return NULL;
    C3D_RenderTargetSetOutput(rt, screen, side,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    return rt;
}

/* C2D_TargetClear clears colour *and depth*; with no depth buffer citro3d
 * builds that second fill from a NULL pointer (C3D_FrameBufClear). Clear the
 * colour only, after the same flush C2D_TargetClear makes first. */
static void clear_target(C3D_RenderTarget* t, u32 color)
{
    C2D_Flush();
    C3D_FrameSplit(0);
    C3D_RenderTargetClear(t, C3D_CLEAR_COLOR, __builtin_bswap32(color), 0);
}

void ttm_gfx3ds_init(void)
{
    /* 3D before the eyes are made; at slider 0 they draw the same picture */
    gfxSet3D(true);
    top = screen_target(GFX_TOP, GFX_LEFT, GSP_SCREEN_HEIGHT_TOP);
    top_r = screen_target(GFX_TOP, GFX_RIGHT, GSP_SCREEN_HEIGHT_TOP);
    frame[0] = frame_target(&frame_tex[0], GPU_RGBA8);
    frame[1] = frame_target(&frame_tex[1], GPU_RGBA8);
    frozen_rt = frame_target_sized(&frozen_tex, GPU_RGB565, FROZEN_W, FROZEN_H);
    if (!frame[1]) gfx_log("gfx: no VRAM for a second frame: screen tone not drawn");
    if (!frozen_rt) gfx_log("gfx: no VRAM for the frozen frame: transitions not drawn");
    if (!top) gfx_log("gfx: no VRAM for the top screen");
    if (!top_r) {
        gfx_log("gfx: no VRAM for the right eye: 3D off");
        gfxSet3D(false);
    }
    char line[96];
    snprintf(line, sizeof(line), "gfx: VRAM %lu KB free after setup (3D %s)",
             (unsigned long)(vramSpaceFree() / 1024), (gfxIs3D() && top_r) ? "on" : "off");
    gfx_log(line);
}

/* ---- custom passes: citro2d's quads with our own combiners ------------------------- */

static void blend_off(void)
{
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    blend_now = -1;
}

/* the part (x, y, w, h) of a frame-sized texture, drawn at the same place */
static void draw_frame_part(C3D_Tex* t, int x, int y, int w, int h, const C2D_ImageTint* tint)
{
    Tex3DS_SubTexture sub = { (u16)w, (u16)h, x / 1024.0f, 1.0f - y / 512.0f,
                              (x + w) / 1024.0f, 1.0f - (y + h) / 512.0f };
    C2D_Image img = { t, &sub };
    C2D_DrawImageAt(img, (float)x, (float)y, 0.0f, tint, 1.0f, 1.0f);
}

/* the frozen frame (512x256, the whole game frame) at the frame's 640x480:
 * its UVs cover the whole texture, its quad is the 640x480 the game drew */
static void draw_frozen(const C2D_ImageTint* tint)
{
    Tex3DS_SubTexture sub = { 640, 480, 0.0f, 1.0f, 1.0f, 0.0f };
    C2D_Image img = { &frozen_tex, &sub };
    C2D_DrawImageAt(img, 0.0f, 0.0f, 0.0f, tint, 1.0f, 1.0f);
}

/* combiners for the quad just queued: texture toned, alpha = texture alpha
 * times `alpha`. Stages: 0-2 luma, 3 toward it by gray, 4 + the positive
 * part of the tone, 5 - the negative part. */
static void tone_env(const int16_t t[4], int alpha)
{
    int pos[3], neg[3];
    for (int i = 0; i < 3; ++i) {
        pos[i] = t[i] > 0 ? t[i] : 0;
        neg[i] = t[i] < 0 ? -t[i] : 0;
    }
    int g = t[3];
    C3D_TexEnv* e = C3D_GetTexEnv(0);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_R, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(e, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvOpAlpha(e, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
    C3D_TexEnvColor(e, C2D_Color32(76, 76, 76, (u8)alpha));          /* 0.299 */

    e = C3D_GetTexEnv(1);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(e, C2D_Color32(150, 150, 150, 255));               /* 0.587 */

    e = C3D_GetTexEnv(2);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(e, C2D_Color32(29, 29, 29, 255));                  /* 0.114 */

    e = C3D_GetTexEnv(3);                  /* luma * g + texture * (1 - g) */
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvColor(e, C2D_Color32((u8)g, (u8)g, (u8)g, 255));

    e = C3D_GetTexEnv(4);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_ADD);
    C3D_TexEnvColor(e, C2D_Color32((u8)pos[0], (u8)pos[1], (u8)pos[2], 255));

    e = C3D_GetTexEnv(5);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_SUBTRACT);
    C3D_TexEnvColor(e, C2D_Color32((u8)neg[0], (u8)neg[1], (u8)neg[2], 255));
}

/* after a custom pass: citro2d's own combiners, depth test and blending back */
static void c2d_restore(void)
{
    for (int i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C2D_Prepare();
    blend_now = -1;
    set_blend(0);
}

void gfx_quad_tone(const int16_t* tone)
{
    quad_toned = tone != NULL;
    if (tone) memcpy(quad_tone_v, tone, sizeof(quad_tone_v));
}

void gfx_region_tone(int x, int y, int w, int h, const int16_t tone[4])
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 640) w = 640 - x;
    if (y + h > 480) h = 480 - y;
    if (!frame[1] || w <= 0 || h <= 0) return;
    int src = cur, dst = cur ^ 1;
    C2D_SceneBegin(frame[dst]);        /* flushes what was drawn into src */
    blend_off();
    if (x > 0 || y > 0 || w < 640 || h < 480) {    /* the rest of the frame as it is */
        draw_frame_part(&frame_tex[src], 0, 0, 640, 480, NULL);
        C2D_Flush();
    }
    draw_frame_part(&frame_tex[src], x, y, w, h, NULL);
    tone_env(tone, 255);
    C2D_Flush();
    c2d_restore();
    cur = dst;
}

void gfx_region_fill(int x, int y, int w, int h, const uint8_t c[4])
{
    set_blend(0);
    C2D_DrawRectSolid((float)x, (float)y, 0.0f, (float)w, (float)h, C2D_Color32(c[0], c[1], c[2], c[3]));
}

void gfx_freeze(void) { freeze_pending = 1; }

void gfx_transition_mask(const uint8_t* mask)
{
    mask_on = 0;
    if (!mask) return;
    if (!mask_tex_ok) {
        if (!C3D_TexInit(&mask_tex, 1024, 512, GPU_A8)) return;
        C3D_TexSetFilter(&mask_tex, GPU_NEAREST, GPU_NEAREST);
        mask_tex_ok = 1;
    }
    if (!mask_tiled && !(mask_tiled = (uint8_t*)malloc(1024 * 512))) return;
    memset(mask_tiled, 0, 1024 * 512);
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x) mask_tiled[tiled_index((u32)x, (u32)y, 1024)] = mask[y * 640 + x];
    mask_on = 1;
}

void gfx_frozen_draw(int alpha, const uint8_t* lut)
{
    if (!frozen_ok) return;
    if (lut && mask_on) {
        uint8_t* d = (uint8_t*)mask_tex.data;
        for (int i = 0; i < 1024 * 512; ++i) d[i] = lut[mask_tiled[i]];
        C3D_TexFlush(&mask_tex);
        /* the frame's alpha := the frozen frame's opacity, pixel by pixel */
        blend_off();
        draw_frame_part(&mask_tex, 0, 0, 640, 480, NULL);
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALPHA);
        C2D_Flush();
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
        /* the frozen frame over it by that alpha; alpha back to opaque */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA, GPU_ONE, GPU_ZERO);
        draw_frozen(NULL);
        C2D_Flush();
        blend_now = -1;
        set_blend(0);
        return;
    }
    if (alpha <= 0) return;
    set_blend(0);
    C2D_ImageTint it;
    C2D_PlainImageTint(&it, C2D_Color32(255, 255, 255, (u8)(alpha > 255 ? 255 : alpha)), 0.0f);
    draw_frozen(&it);
}

/* ---- frame pacing ------------------------------------------------------------------
 * RGSS runs the game one update per frame at Graphics.frame_rate (40), so the
 * game is only as fast as its frames: at 30 fps everything moves at 75 %.
 * Frames are paced by the clock, not by the screen's 60 Hz refresh: syncing to
 * it rounded every frame that took more than 16.7 ms up to 33.3 ms, i.e. a
 * flat 30 fps (console run 5, the house). And when a frame would be late
 * anyway, graphics.c skips drawing it (gfx_should_skip) and runs only the
 * game's update, as RGSS does, so the game keeps its speed and a heavy scene
 * shows fewer frames instead of slow motion. */

static u64 next_frame;      /* when the last frame shown was due */
static u64 frame_start;     /* gfx_frame_begin of the frame being drawn */
static u64 draw_cost;       /* ticks the last drawn frame took to draw */

static u64 frame_period(void)
{
    extern int ttm_frame_rate(void);
    return SYSCLOCK_ARM11 / (u64)ttm_frame_rate();
}

int gfx_should_skip(void)
{
    u64 now = svcGetSystemTick(), p = frame_period();
    if (!next_frame) return 0;
    if (now > next_frame + 4 * p) {      /* a long stall (a map loading): start afresh */
        next_frame = now;
        return 0;
    }
    return now + draw_cost > next_frame + p;
}

void gfx_frame_skipped(void) { next_frame += frame_period(); }

/* the game calls Graphics.frame_reset after work it knows stalls (a map's
 * panorama, a BGM): without it the frames after a stall were run back to back
 * to catch up, and the game visibly sped up */
void gfx_frame_reset(void) { next_frame = 0; }

void gfx_frame_begin(void)
{
    frame_start = svcGetSystemTick();
    C3D_FrameBegin(0);
    blend_now = -1;
    if (freeze_pending && frozen_rt) {    /* the frame last shown, before it is drawn over */
        C2D_SceneBegin(frozen_rt);
        blend_off();
        /* 640x480 into the 512x256 the frozen frame keeps (the top screen
         * shows at most 400x240, so the fade looks the same) */
        Tex3DS_SubTexture part = { 640, 480, 0.0f, 1.0f, 640.0f / 1024.0f, 1.0f - 480.0f / 512.0f };
        C2D_Image f = { &frame_tex[cur], &part };
        C2D_DrawImageAt(f, 0.0f, 0.0f, 0.0f, NULL, (float)FROZEN_W / 640.0f, (float)FROZEN_H / 480.0f);
        C2D_Flush();
        frozen_ok = 1;
    }
    freeze_pending = 0;
    cur = 0;
    C2D_SceneBegin(frame[0]);
    /* cleared by drawing, not C2D_TargetClear: a clear is a memory fill that
     * can run before the draws queued above (the freeze copy reads frame[0]) */
    blend_off();
    C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, 640.0f, 480.0f, C2D_Color32(0, 0, 0, 255));
    set_blend(0);
}

void gfx_clip(int x, int y, int w, int h)
{
    /* unrotated quads arrive clipped (ttm_draw); rotated ones are not clipped */
    (void)x; (void)y; (void)w; (void)h;
}

void gfx_quad(gfx_tex* t, const gfx_q* q, int alpha, int blend, const uint8_t tint[4])
{
    if (!t || !t->ok) return;
    int toned = quad_toned;
    Tex3DS_SubTexture sub;
    sub.width = (u16)(q->sw + 0.5f);
    sub.height = (u16)(q->sh + 0.5f);
    sub.left = q->sx / t->pw;
    sub.right = (q->sx + q->sw) / t->pw;
    sub.top = 1.0f - q->sy / t->ph;
    sub.bottom = 1.0f - (q->sy + q->sh) / t->ph;
    C2D_Image img = { &t->tex, &sub };
    if (in_bottom && !t->linear && n_linear < MAX_LINEAR) {
        C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
        t->linear = 1;
        linear_texs[n_linear++] = t;
    }
    C2D_DrawParams p;
    p.pos.x = q->x;
    p.pos.y = q->y;
    p.pos.w = q->mirror ? -q->w : q->w;
    p.pos.h = q->h;
    p.center.x = 0;
    p.center.y = 0;
    p.depth = 0;
    p.angle = -q->angle;          /* citro2d turns clockwise on screen, RGSS counter-clockwise */
    set_blend(blend);
    if (toned) {    /* its own combiners: flush what came before, then this quad alone */
        C2D_Flush();
        C2D_DrawImage(img, &p, NULL);
        tone_env(quad_tone_v, alpha);
        C2D_Flush();
        c2d_restore();
        return;
    }
    C2D_ImageTint it;
    C2D_PlainImageTint(&it, C2D_Color32(tint[0], tint[1], tint[2], (u8)alpha), tint[3] / 255.0f);
    C2D_DrawImage(img, &p, &it);
}

/* ---- the bottom screen ------------------------------------------------------------------ */

static C3D_RenderTarget* bottom;

void ttm_bottom_start(void)
{
    if (bottom) return;
    /* the console left the bottom screen single-buffered RGB565 */
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    bottom = screen_target(GFX_BOTTOM, GFX_LEFT, GSP_SCREEN_HEIGHT_BOTTOM);
    if (!bottom) {
        gfx_log("gfx: no VRAM for the bottom screen");
        return;
    }
    /* one cleared frame at once, in the menu's own dark colour: the console's
     * boot text must not stay up until the game's first frame */
    C3D_FrameBegin(0);
    clear_target(bottom, C2D_Color32(18, 20, 34, 255));
    C2D_SceneBegin(bottom);
    C3D_FrameEnd(0);
    char line[96];
    snprintf(line, sizeof(line), "gfx: VRAM %lu KB free with the bottom screen",
             (unsigned long)(vramSpaceFree() / 1024));
    gfx_log(line);
}

void ttm_bottom_stop(void)
{
    if (!bottom) return;
    C3D_RenderTargetDelete(bottom);
    bottom = NULL;
    consoleInit(GFX_BOTTOM, NULL);
}

int gfx_bottom_begin(void)
{
    if (!bottom) return 0;
    clear_target(bottom, C2D_Color32(18, 20, 34, 255));
    C2D_SceneBegin(bottom);    /* flushes the top screen's frame first */
    /* citro2d sends a texture's filter only when it binds that texture
     * again: forget the bound one, here and after the pass */
    c2d_restore();
    in_bottom = 1;
    n_linear = 0;
    return 1;
}

void gfx_bottom_end(int dim)
{
    if (dim > 0) {
        set_blend(0);
        C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, (float)TTM_BOTTOM_W, (float)TTM_BOTTOM_H, C2D_Color32(0, 0, 0, (u8)dim));
    }
    /* the pass's draws carry the linear filter into the command list when
     * flushed; after that the textures can go back to nearest */
    C2D_Flush();
    for (int i = 0; i < n_linear; ++i) {
        C3D_TexSetFilter(&linear_texs[i]->tex, GPU_NEAREST, GPU_NEAREST);
        linear_texs[i]->linear = 0;
    }
    n_linear = 0;
    in_bottom = 0;
    c2d_restore();
}

/* a texture freed while in the list (cannot happen within a pass; kept safe) */
static void bottom_forget(gfx_tex* t)
{
    for (int i = 0; i < n_linear; ++i)
        if (linear_texs[i] == t) linear_texs[i--] = linear_texs[--n_linear];
    t->linear = 0;
}

void gfx_frame_end(void)
{
    C2D_Flush();
    set_blend(0);
    static const Tex3DS_SubTexture whole = { 640, 480, 0.0f, 1.0f, 640.0f / 1024.0f, 1.0f - 480.0f / 512.0f };
    C2D_Image img = { &frame_tex[cur], &whole };
    /* the 3D slider scales the parallax; both eyes are drawn every frame, so
     * at slider 0 they are identical and the picture is flat */
    float d = osGet3DSliderState() * TTM_DEPTH_PX * 0.5f;
    C3D_RenderTarget* eyes[2] = { top, top_r };
    for (int e = 0; e < 2; ++e) {
        if (!eyes[e]) continue;
        float off = e == 0 ? -d : d;
        clear_target(eyes[e], C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(eyes[e]);
        if (ttm_top_stretch)
            C2D_DrawImageAt(img, off, 0.0f, 0.0f, NULL, 400.0f / 640.0f, 240.0f / 480.0f);
        else
            C2D_DrawImageAt(img, 40.0f + off, 0.0f, 0.0f, NULL, 0.5f, 0.5f);
    }
    C3D_FrameEnd(0);
    u64 now = svcGetSystemTick();
    draw_cost = now - frame_start;

    /* each second: frames drawn / game updates, then per update the game's
     * Ruby time, per drawn frame the drawing time, and free texture memory --
     * to log.txt every fifth second, and any second whose Ruby time per
     * update was over 100 ms (a slow spot: console run 12's choices) */
    static u64 second_start;
    static int seconds;
    extern int ttm_stat_updates, ttm_stat_drawn;
    extern uint64_t ttm_stat_ruby_us, ttm_stat_draw_us;
    if (!second_start) second_start = now;
    if (now - second_start >= SYSCLOCK_ARM11) {
        int u = ttm_stat_updates > 0 ? ttm_stat_updates : 1, d = ttm_stat_drawn > 0 ? ttm_stat_drawn : 1;
        double rb = ttm_stat_ruby_us / 1000.0 / u;
        if (ttm_gfx_log_fn && (++seconds % 5 == 0 || rb > 100.0)) {
            char line[128];
            snprintf(line, sizeof(line), "perf:%s %d/%d fps, rb %.1f, dr %.1f ms, tx %ldK", rb > 100.0 ? " slow," : "",
                     ttm_stat_drawn, ttm_stat_updates, rb, ttm_stat_draw_us / 1000.0 / d, gfx_tex_mem_free_kb());
            ttm_gfx_log_fn(line);
        }
        ttm_stat_updates = ttm_stat_drawn = 0;
        ttm_stat_ruby_us = ttm_stat_draw_us = 0;
        second_start = now;
    }

    /* pace to Graphics.frame_rate; when behind, stay behind (so the next
     * frames can be skipped to catch up) unless it is a long stall */
    u64 p = frame_period();
    if (!next_frame) next_frame = now;
    next_frame += p;
    if (now < next_frame)
        svcSleepThread((s64)((next_frame - now) * 1000000000ULL / SYSCLOCK_ARM11));
    else if (now > next_frame + 4 * p)
        next_frame = now;
}

const uint32_t* gfx_frame_pixels(void) { return NULL; }

int gfx_running(void) { return aptMainLoop(); }

#endif /* __3DS__ */
