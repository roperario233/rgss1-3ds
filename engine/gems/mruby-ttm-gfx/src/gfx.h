/*
 * gfx.h -- internals of mruby-ttm-gfx, RGSS1's graphics classes in C.
 *
 * The game draws on a 640x480 virtual screen, exactly as on the PC. Each frame
 * Graphics.update walks every live Viewport, Sprite and Window in RGSS's z order
 * and hands the backend a list of textured quads. Two backends:
 *   backend_soft.c  the PC: a software rasterizer, so frames can be saved as PNG
 *                   and checked without a console
 *   backend_c3d.c   the 3DS: citro3d (added with the 3DS engine app)
 * Everything else here -- bitmaps, text, window layout, z order -- is shared.
 *
 * Pixels are 32-bit RGBA in memory order R,G,B,A (0xAABBGGRR as a uint32 on a
 * little-endian CPU), which is what stb_image produces.
 */
#ifndef TTM_GFX_H
#define TTM_GFX_H

#include <stdint.h>

#include <mruby.h>
#include <mruby/data.h>

#include "ttm_values.h"

#define TTM_SCREEN_W 640
#define TTM_SCREEN_H 480

/* ---- backend ---------------------------------------------------------------- */

typedef struct gfx_tex gfx_tex;

/* A textured quad: texture rect (sx,sy,sw,sh) shown on a dst rect whose
 * top-left is (x,y) and size (w,h), rotated `angle` radians counter-clockwise
 * about that top-left, and mirrored (texture flipped left-right in place). */
typedef struct {
    float sx, sy, sw, sh;
    float x, y, w, h;
    float angle;
    int mirror;
} gfx_q;

gfx_tex* gfx_tex_new(int w, int h);
struct ttm_bitmap;
/* fill the whole texture (w x h as created) from the bitmap's pixels whose
 * top-left is (x0, y0) -- a slice of a larger bitmap uploads in place */
void gfx_tex_upload(gfx_tex* t, const struct ttm_bitmap* b, int x0, int y0);
void gfx_tex_free(gfx_tex* t);
/* 0 when gfx_tex_new could not get the memory (the texture draws nothing) */
int gfx_tex_ok(const gfx_tex* t);
/* bytes the texture holds in texture memory (power-of-two padded on the 3DS) */
long gfx_tex_bytes(const gfx_tex* t);
/* PC only: pretend texture memory is `kb` (0 = unlimited), padded the way the
 * 3DS pads, so running out of it can be reproduced off the console */
void gfx_tex_budget(long kb);
/* The largest texture side this backend can draw (0 = no limit). A bitmap
 * larger than that is held as a grid of textures (slices) of at most this
 * size (3DS: 1024, the PICA200 limit; PC: unlimited). */
int gfx_max_tex_dim(void);
/* Free main RAM in KB, or 0 when the platform cannot say. Diagnostics only:
 * this is what tells us whether a failed decode really was out of memory. */
long gfx_mem_free_kb(void);
/* The largest block malloc can give now, in KB (0 = cannot say). Probed by
 * allocating, so it is for failure log lines only: free RAM in pieces too
 * small to use is what a fragmented heap looks like (console run 9). */
long gfx_mem_largest_kb(void);
/* Free texture memory in KB (3DS: the linear heap), or 0 when unknown. */
long gfx_tex_mem_free_kb(void);

void gfx_frame_begin(void);
/* clip rectangle for rotated quads (unrotated ones arrive already clipped) */
void gfx_clip(int x, int y, int w, int h);
/* alpha 0..255 multiplies the texture's alpha; blend 0 normal, 1 add, 2 sub;
 * tint (r,g,b,a 0..255): the texel's colour moves toward rgb by a/255 */
void gfx_quad(gfx_tex* t, const gfx_q* q, int alpha, int blend, const uint8_t tint[4]);
void gfx_frame_end(void);
/* ---- tone, viewport effects, freeze and transition ----------------------------
 * A tone (RGSS) is r, g, b (-255..255) and gray (0..255): each pixel moves
 * toward its luma (0.299 R + 0.587 G + 0.114 B) by gray/255, then r, g, b
 * are added. 826 of the game's 989 screen-tone changes use gray. */

/* the tone of the quads drawn next (a sprite's or a plane's); NULL: none.
 * A toned quad ignores its tint (colour): the 3DS's six combiner stages go
 * to the tone. */
void gfx_quad_tone(const int16_t* tone);
/* tone everything drawn so far inside (x, y, w, h) of the frame: a viewport's
 * tone, once its contents are drawn (as mkxp: what lies under it too) */
void gfx_region_tone(int x, int y, int w, int h, const int16_t tone[4]);
/* rgba over (x, y, w, h), alpha-blended: a viewport's colour or flash */
void gfx_region_fill(int x, int y, int w, int h, const uint8_t rgba[4]);
/* Graphics.freeze: keep the frame last shown (3DS: taken as the next frame
 * begins, before it is cleared) */
void gfx_freeze(void);
/* a transition image, TTM_SCREEN_W x TTM_SCREEN_H values 0..255 (NULL: none),
 * for the gfx_frozen_draw calls that follow */
void gfx_transition_mask(const uint8_t* mask);
/* draw the kept frame over the frame being drawn: at opacity alpha (0..255),
 * or, with a lut and a mask set, each pixel at opacity lut[its mask value] */
void gfx_frozen_draw(int alpha, const uint8_t* lut);

/* frame skip (RGSS's): 1 when drawing the coming frame would make it late, so
 * Graphics.update runs the game's update without drawing; the backend is then
 * told the frame went by (PC: never -- every frame is drawn) */
int gfx_should_skip(void);
void gfx_frame_skipped(void);
/* ---- the bottom screen ---------------------------------------------------------
 * Drawables made inside Graphics.ttm_on_bottom belong to the bottom screen
 * (320x240; the game's menu, romfs/rgss/ttm_patches.rb). graphics.c draws them
 * in a second pass, mapping game space to the screen through the bottom view
 * (Graphics.ttm_bottom_view). gfx_bottom_begin returns 0 when there is no
 * bottom screen to draw (3DS: it is the text console). gfx_bottom_end puts a
 * black veil of opacity dim (0..255) over it. */
#define TTM_BOTTOM_W 320
#define TTM_BOTTOM_H 240
int gfx_bottom_begin(void);
void gfx_bottom_end(int dim);
/* Graphics.frame_reset, and a return from the HOME menu: pace from now on,
 * without catching up time already lost (3DS; nothing to do on the PC) */
void gfx_frame_reset(void);
/* 0 when the platform wants the app to close (3DS: HOME > close, power) */
int gfx_running(void);
/* Graphics.frame_rate, for the backend's frame pacing */
int ttm_frame_rate(void);
/* PC only: the last frame as RGBA, for screenshots and Graphics.snap_to_bitmap */
const uint32_t* gfx_frame_pixels(void);
/* PC only: the bottom screen's last frame as a PNG (0 on the 3DS) */
int gfx_save_png_bottom(const char* path);

/* ---- Bitmap -------------------------------------------------------------------- */

typedef struct ttm_bitmap {
    int w, h;
    /* w x h RGBA, in bands of 1 << bshift rows, each band its own allocation
     * of at most TTM_BAND_BYTES: no bitmap needs one large block of RAM. A
     * 256x6096 tileset in one piece was 6 MB that a fragmented heap could not
     * find with 40 MB free (console run 8); as bands it is 48 blocks of 128 KB.
     * Row y is ttm_row(b, y); NULL bands = no pixels: disposed, or (file
     * still set) shed to free RAM -- ttm_bitmap_pixels decodes them again. */
    uint32_t** bands;
    int nbands, bshift;
    /* backend copy, made on first draw: tcols x trows textures on a grid of
     * tgrid px (the last row and column smaller). tdim is the limit: the
     * backend's largest texture (0: none), and tgrid the size at or under it
     * that pads least to powers of two. A tileset 256x9332 is ten slices. */
    gfx_tex** tiles;
    int tcols, trows, tdim, tgrid;
    /* texture memory is a cache over px: every bitmap holding textures is on
     * one list, and the ones not drawn lately give theirs up when it runs out */
    struct ttm_bitmap *tprev, *tnext;
    long tbytes;
    uint32_t tframe;    /* the frame it was last drawn in */
    uint32_t serial;    /* unique per bitmap made: identity that survives malloc reusing an address */
    int dirty;          /* px changed since the last upload */
    int disposed;
    /* the image file px was decoded from, while px is still exactly that
     * (NULL once anything draws into it): a dispose keeps such pixels for the
     * next Bitmap.new of the same file (bitmap.c, "kept images"), and lets
     * RAM run short shed them, since they can be decoded again (bitmap.c,
     * "decoded pixels are a cache too") */
    char* file;
    /* the bitmaps whose pixels could be shed, most recently used first */
    struct ttm_bitmap *rprev, *rnext;
    int on_res;
    uint32_t pused;     /* the frame its pixels were last read in */
} ttm_bitmap;

/* 64 KB, not 128: console run 9 failed to find 80 KB with 9.5 MB free */
#define TTM_BAND_BYTES (64 * 1024)

/* row y of b: w pixels, contiguous (a row never spans two bands) */
static inline uint32_t* ttm_row(const ttm_bitmap* b, int y)
{
    return b->bands[y >> b->bshift] + (size_t)(y & ((1 << b->bshift) - 1)) * (size_t)b->w;
}

/* give b (which has none) w x h transparent pixels, in bands; 0 when RAM ran
 * out, b then holding none */
int ttm_bitmap_alloc(ttm_bitmap* b, int w, int h);
/* free b's pixels (the other half of ttm_bitmap_alloc) */
void ttm_bitmap_free_px(ttm_bitmap* b);
/* make sure b has its pixels, decoding its file again if they were shed;
 * 0 when it has none (disposed, or no RAM for them now) */
int ttm_bitmap_pixels(ttm_bitmap* b);

/* png.c: a PNG decoded row by row straight into b's bands, so a decode needs
 * no more than the pixels themselves (stb inflates the whole image into one
 * buffer first, then makes the pixels in another). 1: decoded; 0: not a PNG
 * this decoder takes (interlaced, 16-bit) -- use stb; -1: failed, *why says
 * why ("outofmem", "corrupt PNG"). */
int ttm_png_info(const unsigned char* data, size_t len, int* w, int* h);
int ttm_png_decode(const unsigned char* data, size_t len, ttm_bitmap* b, const char** why);

extern const mrb_data_type ttm_bitmap_type;
ttm_bitmap* ttm_bitmap_ptr(mrb_value v);            /* NULL if not a live Bitmap */
/* draw q's texture rect of b (uploading first if dirty), split across b's
 * slices: every caller draws bitmaps through this, so no caller knows about
 * slices */
void ttm_bitmap_draw(ttm_bitmap* b, gfx_q q, int alpha, int blend, const uint8_t tint[4]);
/* a bitmap with no Ruby object, owned by C code (the tilemap's autotile
 * patterns): w x h transparent pixels, drawn with ttm_bitmap_draw */
ttm_bitmap* ttm_bitmap_new_raw(int w, int h);
void ttm_bitmap_destroy(ttm_bitmap* b);
/* textures held by bitmaps: how many bitmaps, their bytes, and how many times
 * a texture could not be made even after freeing others (Graphics.ttm_tex_stats) */
void ttm_bitmap_tex_stats(long* count, long* bytes, long* failed);
/* free the decoded images kept after a dispose (graphics.c: when RAM is low) */
void ttm_bitmap_unkeep_all(void);
/* between frames: shed image pixels until RAM is back over the reserve
 * (free_kb: gfx_mem_free_kb(), already read) */
void ttm_bitmap_relieve(long free_kb);
/* pixels in RAM: all bitmaps' bytes, the part that could be shed, how many
 * images were shed and decoded again, and reloads that failed */
void ttm_bitmap_ram_stats(long* bytes, long* sheddable, long* shed, long* reloaded, long* failed);
/* "N KB free, largest M KB, P KB px" for log lines: what RAM looked like */
void ttm_bitmap_ram_note(char* out, size_t n);
/* the frame being drawn: Graphics.update counts them (graphics.c) */
extern uint32_t ttm_render_frame;

/* ---- text (font.c) ---------------------------------------------------------------- */

void ttm_font_init(mrb_state* mrb);
/* width in pixels of str at size px, in the named face ("" = the default) */
int ttm_text_width(const char* font_name, const char* str, int len, int size);
int ttm_text_height(const char* font_name, int size);
/* draw str into b, inside rect (x,y,w,h), align 0 left 1 centre 2 right,
 * squeezed horizontally if it does not fit */
void ttm_text_draw(ttm_bitmap* b, const char* font_name, int x, int y, int w, int h,
                   const char* str, int len, int size, int bold, const uint8_t color[4],
                   int align);

/* ---- drawables: Viewport, Sprite, Window, registered for z order ---------------- */

enum { TTM_VIEWPORT = 1, TTM_SPRITE, TTM_WINDOW, TTM_TILEMAP_GROUND, TTM_TILEMAP_ROW, TTM_PLANE };

typedef struct ttm_drawable {
    int kind;
    struct RData* self;     /* the Ruby object (ivars hold its bitmap, viewport, ...) */
    uint32_t id;            /* creation order: ties in z go to the newer object */
    int z;
    int visible;
    int disposed;
    int screen;             /* 0 the top screen (the game), 1 the bottom (Graphics.ttm_on_bottom) */
} ttm_drawable;

void ttm_register(ttm_drawable* d, int kind, struct RData* self);
void ttm_unregister(ttm_drawable* d);
/* z= : as in mkxp (and RGSS), an object whose z changes goes on top of the
 * objects that already have that z; setting the same z again changes nothing */
void ttm_set_z(ttm_drawable* d, int z);

typedef struct {
    ttm_drawable d;
    int ox, oy;
    uint8_t flash[4];
    int flash_count, flash_duration;
} ttm_viewport;

/* draw one drawable; ox/oy and clip come from its viewport (or the screen) */
void ttm_sprite_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int ox, int oy);
void ttm_window_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int ox, int oy);
void ttm_tilemap_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int ox, int oy);
void ttm_plane_draw(mrb_state* mrb, ttm_drawable* d, int vx, int vy, int ox, int oy);
/* the viewport a drawable belongs to (nil if none) */
mrb_value ttm_drawable_viewport(mrb_state* mrb, ttm_drawable* d);

/* clipping inside the current viewport: narrow to (x,y,w,h), then restore */
void ttm_clip_narrow(int x, int y, int w, int h);
void ttm_clip_restore(void);
/* the current clip rectangle, [x0,x1) x [y0,y1): what a Plane has to cover */
void ttm_clip_current(int* x0, int* y0, int* x1, int* y1);

/* draw through the current clip: unrotated quads are trimmed here (texture
 * rect adjusted to match), so backends need no clipping of their own */
void ttm_draw(gfx_tex* t, gfx_q q, int alpha, int blend, const uint8_t tint[4]);

/* class registration */
void ttm_init_bitmap(mrb_state* mrb);
void ttm_init_viewport(mrb_state* mrb);
void ttm_init_sprite(mrb_state* mrb);
void ttm_init_window(mrb_state* mrb);
void ttm_init_tilemap(mrb_state* mrb);
void ttm_init_plane(mrb_state* mrb);
void ttm_init_graphics(mrb_state* mrb);

/* small helpers shared by the class files */
mrb_int ttm_to_int(mrb_state* mrb, mrb_value v);
double ttm_to_dbl(mrb_state* mrb, mrb_value v);
mrb_value ttm_iv(mrb_state* mrb, struct RData* self, const char* name);
void ttm_color_bytes(mrb_value color, uint8_t out[4]);  /* Color -> r,g,b,a; zeros if nil */
/* Tone -> r, g, b (-255..255), gray (0..255); 0 if nil or all zero (nothing to draw) */
int ttm_tone_values(mrb_value tone, int16_t out[4]);

#endif
