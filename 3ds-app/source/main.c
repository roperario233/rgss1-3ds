/*
 * ttm3ds_test -- M1 hardware bring-up test for the To the Moon 3DS port.
 *
 * What it proves on real hardware:
 *   - the devkitARM/libctru/citro2d toolchain and a 3DSX build work
 *   - raw textures load from romfs and upload to the PICA200 correctly
 *   - the two candidate screen layouts (matrix §5.2 options A and C) can be
 *     compared by eye on the actual panels
 *   - a frame-time counter runs, so we get a first fps number
 *   - the New 3DS 804 MHz clock mode can be enabled and confirmed
 *
 * It deliberately contains NO PNG decoder and NO font library: every screen was
 * pre-composed on the PC by tools/make_ui_mockup.py and packed as raw RGBA by
 * 3ds-app/build_assets.py, so the only thing under test here is the 3DS side.
 *
 * Hardware details that matter and are handled below:
 *   - PICA200 textures must be POWER-OF-TWO, so each image is allocated at the
 *     next POT size (320x240 and 400x240 -> 512x256, 640x480 -> 1024x512) and
 *     the drawn area is selected with UVs (Tex3DS_SubTexture).
 *   - PICA200 textures are TILED, not row-major: 8x8 tiles with the texels of
 *     each tile in Morton order, and RGBA8 stored as A,B,G,R bytes. The .ttmtex
 *     files are plain RGBA rows, so tex_load() swizzles them on the way in.
 *   - 640x480 also exceeds the 1024-pixel texture limit in neither dimension
 *     only after rounding up to 1024x512 -- which is exactly why the matrix
 *     calls the 1024 px limit a design constraint.
 *
 * Controls:  LEFT/RIGHT or A = next mode, START = exit.
 * The top-left of the bottom screen shows the mode number and the fps.
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP_W   400
#define TOP_H   240
#define BOT_W   320
#define BOT_H   240

#define MODE_COUNT 5

typedef struct {
    C3D_Tex tex;
    Tex3DS_SubTexture sub;   /* citro2d uses tex3ds' sub-texture type */
    int w, h;              /* logical size */
    int pot_w, pot_h;      /* allocated size */
    bool ok;
} Texture;

/* ------------------------------------------------------------------ */
/* texture loading                                                     */
/* ------------------------------------------------------------------ */

static int next_pot(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

/* Index of texel (x, y) in a PICA200 tiled texture: 8x8 tiles left-to-right,
 * top-to-bottom, and Morton (Z) order inside each tile with x in the even bits.
 * Image row 0 lands in tile row 0, which the GPU samples at v = 1.0 -- hence
 * top = 1.0 in every sub-texture below. */
static inline u32 tiled_index(u32 x, u32 y, u32 pot_w)
{
    u32 tile = (y >> 3) * (pot_w >> 3) + (x >> 3);
    u32 morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
                 ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return (tile << 6) + morton;
}

static bool tex_load(Texture* t, const char* name, GPU_TEXTURE_FILTER_PARAM filter)
{
    char path[160];

    /* Prefer the romfs packed into the build. A CIA install can carry no romfs,
     * in which case the textures come from the SD card instead -- which is also
     * the model the real port will use for the player's own game data. */
    snprintf(path, sizeof(path), "romfs:/%s", name);
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "sdmc:/3ds/ttm3ds_test/%s", name);
        f = fopen(path, "rb");
    }
    if (!f) return false;

    char magic[8];
    u32 w = 0, h = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TTMRGBA1", 8) != 0) {
        fclose(f);
        return false;
    }
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1) {
        fclose(f);
        return false;
    }
    if (w == 0 || h == 0 || w > 1024 || h > 1024) {
        fclose(f);
        return false;
    }

    t->pot_w = next_pot((int)w);
    t->pot_h = next_pot((int)h);
    t->w = (int)w;
    t->h = (int)h;

    /* The texture lives in linear memory, so swizzle straight into it; the
     * padding outside the image stays transparent black. */
    if (!C3D_TexInit(&t->tex, (u16)t->pot_w, (u16)t->pot_h, GPU_RGBA8)) {
        fclose(f);
        return false;
    }
    u32* texels = (u32*)t->tex.data;
    memset(texels, 0, (size_t)t->pot_w * (size_t)t->pot_h * 4);

    const size_t src_stride = (size_t)w * 4;
    u8* row = (u8*)malloc(src_stride);
    if (!row) {
        C3D_TexDelete(&t->tex);
        fclose(f);
        return false;
    }
    for (u32 y = 0; y < h; ++y) {
        if (fread(row, 1, src_stride, f) != src_stride) {
            free(row);
            C3D_TexDelete(&t->tex);
            fclose(f);
            return false;
        }
        for (u32 x = 0; x < w; ++x) {
            /* file bytes are R,G,B,A; GPU_RGBA8 wants the word 0xRRGGBBAA */
            const u8* p = row + x * 4;
            texels[tiled_index(x, y, (u32)t->pot_w)] =
                ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    }
    free(row);
    fclose(f);

    C3D_TexFlush(&t->tex);
    C3D_TexSetFilter(&t->tex, filter, filter);
    C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    t->sub.width = (u16)t->w;
    t->sub.height = (u16)t->h;
    t->sub.left = 0.0f;
    t->sub.top = 1.0f;
    t->sub.right = (float)t->w / (float)t->pot_w;
    t->sub.bottom = 1.0f - (float)t->h / (float)t->pot_h;
    t->ok = true;
    return true;
}

static void tex_free(Texture* t)
{
    if (t->ok) {
        C3D_TexDelete(&t->tex);
        t->ok = false;
    }
}

static void tex_draw(Texture* t, float x, float y, float scale)
{
    if (!t->ok) return;
    C2D_Image img;
    img.tex = &t->tex;
    img.subtex = &t->sub;
    C2D_DrawImageAt(img, x, y, 0.0f, NULL, scale, scale);
}

/* ------------------------------------------------------------------ */
/* baked digit strip (no font library needed)                          */
/* ------------------------------------------------------------------ */

#define DIGIT_W 16
#define DIGIT_H 24

static void draw_number(Texture* digits, int value, float x, float y, float scale)
{
    if (!digits->ok) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    for (int i = 0; buf[i]; ++i) {
        int d = buf[i] - '0';
        if (d < 0 || d > 9) continue;
        /* UVs are fractions of the POT texture (256x32), not of the 160x24 strip */
        Tex3DS_SubTexture sub;
        sub.width = DIGIT_W;
        sub.height = DIGIT_H;
        sub.left = (float)(d * DIGIT_W) / (float)digits->pot_w;
        sub.right = (float)(d * DIGIT_W + DIGIT_W) / (float)digits->pot_w;
        sub.top = 1.0f;
        sub.bottom = 1.0f - (float)DIGIT_H / (float)digits->pot_h;
        C2D_Image img;
        img.tex = &digits->tex;
        img.subtex = &sub;
        C2D_DrawImageAt(img, x + i * (DIGIT_W * scale), y, 0.0f, NULL, scale, scale);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    /* New 3DS: switch to the 804 MHz clock + extra L2. No-op on Old 3DS. */
    osSetSpeedupEnable(true);

    /* System model: 0=3DS 1=3DS XL 2=New 3DS 3=2DS 4=New 3DS XL 5=New 2DS XL */
    u8 model = 0xFF;
    bool new3ds = false;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GetSystemModel(&model);
        cfguExit();
        new3ds = (model == 2 || model == 4 || model == 5);
    }

    romfsInit();
    /* Sets up GSP and the framebuffers. Without it citro3d waits forever for a
     * VBlank that never comes: both screens stay black. */
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    Texture t_a, t_b, t_c, t_full, t_digits;
    memset(&t_a, 0, sizeof(t_a));
    memset(&t_b, 0, sizeof(t_b));
    memset(&t_c, 0, sizeof(t_c));
    memset(&t_full, 0, sizeof(t_full));
    memset(&t_digits, 0, sizeof(t_digits));

    /* 1:1 layouts use NEAREST so pixels stay crisp; the scale test uses LINEAR. */
    bool loaded =
        tex_load(&t_a, "a_option_a.ttmtex", GPU_NEAREST) &&
        tex_load(&t_b, "b_option_b.ttmtex", GPU_NEAREST) &&
        tex_load(&t_c, "c_option_c.ttmtex", GPU_NEAREST) &&
        tex_load(&t_full, "full_frame.ttmtex", GPU_LINEAR) &&
        tex_load(&t_digits, "digits.ttmtex", GPU_NEAREST);

    /* How much application RAM this process was actually given. This is the
     * number that justifies the CIA route: a 3dsx gets the default mode, while a
     * CIA built with SystemModeExt: 178MB gets extended memory on a New 3DS. */
    u32 app_mb = osGetMemRegionSize(MEMREGION_APPLICATION) >> 20;

    int mode = 0;
    u64 last = osGetTime();
    int frames = 0, fps = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;
        if (kDown & (KEY_A | KEY_RIGHT | KEY_DRIGHT))
            mode = (mode + 1) % MODE_COUNT;
        if (kDown & (KEY_B | KEY_LEFT | KEY_DLEFT))
            mode = (mode + MODE_COUNT - 1) % MODE_COUNT;

        ++frames;
        u64 now = osGetTime();
        if (now - last >= 1000) {
            fps = (int)((u64)frames * 1000u / (now - last));
            frames = 0;
            last = now;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);
        if (!loaded) {
            C2D_DrawRectSolid(0, 0, 0, TOP_W, 24, C2D_Color32(220, 0, 0, 255));
        } else {
            switch (mode) {
            case 0:   /* option A: 320x240 centred, 40 px pillarbox each side */
            case 1:   /* option B: same game view, dialogue goes to the bottom */
                tex_draw(&t_a, (TOP_W - t_a.w) / 2.0f, (TOP_H - t_a.h) / 2.0f, 1.0f);
                break;
            case 2:   /* option C: 0.625x, cropped to 400x240 */
                tex_draw(&t_c, 0.0f, 0.0f, 1.0f);
                break;
            case 3:   /* GPU bilinear downscale of the native frame, vs box filter */
                tex_draw(&t_full, (TOP_W - t_full.w * 0.5f) / 2.0f,
                         (TOP_H - t_full.h * 0.5f) / 2.0f, 0.5f);
                break;
            default:  /* reference: native 640x480 at 1:1, cropped */
                tex_draw(&t_full, (TOP_W - t_full.w) / 2.0f,
                         (TOP_H - t_full.h) / 2.0f, 1.0f);
                break;
            }
        }

        C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(bottom);
        if (loaded && mode == 1)
            tex_draw(&t_b, 0.0f, 0.0f, 1.0f);

        /* Status readout, colour-keyed so it needs no font:
         *   red   = mode number
         *   green = frames per second
         *   blue  = application RAM this process was given, in MB
         * (the last one is what the CIA's SystemModeExt changes: 124 -> 178) */
        C2D_DrawRectSolid(0, 0, 0, 64, 92, C2D_Color32(0, 0, 0, 190));
        C2D_DrawRectSolid(4, 6, 0, 6, 16, C2D_Color32(220, 80, 80, 255));
        C2D_DrawRectSolid(4, 34, 0, 6, 16, C2D_Color32(80, 200, 80, 255));
        C2D_DrawRectSolid(4, 62, 0, 6, 16, C2D_Color32(90, 140, 255, 255));
        draw_number(&t_digits, mode, 18, 4, 1.0f);
        draw_number(&t_digits, fps, 18, 32, 1.0f);
        draw_number(&t_digits, (int)app_mb, 18, 60, 1.0f);
        /* green bar = New 3DS family detected (clock boost requested); red = Old */
        C2D_DrawRectSolid(BOT_W - 12, BOT_H - 20, 0, 8, 16,
                          new3ds ? C2D_Color32(0, 200, 0, 255)
                                 : C2D_Color32(200, 0, 0, 255));

        C3D_FrameEnd(0);
    }

    tex_free(&t_a);
    tex_free(&t_b);
    tex_free(&t_c);
    tex_free(&t_full);
    tex_free(&t_digits);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
