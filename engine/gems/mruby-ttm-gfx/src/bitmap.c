/*
 * bitmap.c -- RGSS1 Bitmap.
 *
 * Pixels live on the CPU (games draw text and blit into bitmaps every frame);
 * the backend gets a copy only when a bitmap that changed is next drawn.
 * Image files are read through TTM.image_data(name) (Ruby: the archive first,
 * then plain files, trying RGSS's extensions) and decoded with stb_image.
 *
 * A bitmap larger than the backend's texture limit (gfx_max_tex_dim: the
 * 3DS's 1024 px) is held as a grid of textures, "slices", and drawn piece by
 * piece (ttm_bitmap_draw), so callers never see the limit; each such image is
 * logged once with the memory left. A decode that runs out of RAM becomes a
 * blank bitmap and a log line -- an asset must not end the game (M3 hardware
 * run, matrix 10.8).
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "gfx.h"
#include "stb_image.h"

void ttm_stbi_clear_failure(void);   /* stb_impl.c */

/* ---- texture memory: a cache over the CPU pixels ------------------------------------
 *
 * Every bitmap keeps its pixels in RAM; its textures are only a copy, so they
 * can be dropped and made again. That matters on the 3DS, where textures come
 * from a 48 MB linear heap the garbage collector knows nothing about: the
 * game makes short-lived bitmaps all the time (each message's text, each name
 * tab), and before a collection frees them the heap can run out (console run
 * 4: 44 MB -> 6 MB -> "out of texture memory", and the sprites whose textures
 * failed vanished for good). So: the bitmaps holding textures form a list,
 * most recently drawn first; when a texture cannot be made, the ones not drawn
 * in this frame give theirs up, oldest first, and it is tried again. Only
 * bitmaps not drawn this frame are touched, so nothing the GPU is still to
 * draw is freed. A texture that still cannot be made is retried next frame. */

static ttm_bitmap* tex_head;     /* most recently drawn first */
static long tex_count, tex_bytes, tex_failed;

static void tex_unlink(ttm_bitmap* b)
{
    if (b->tprev) b->tprev->tnext = b->tnext;
    else if (tex_head == b) tex_head = b->tnext;
    if (b->tnext) b->tnext->tprev = b->tprev;
    b->tprev = b->tnext = NULL;
}

static void tex_link_front(ttm_bitmap* b)
{
    b->tprev = NULL;
    b->tnext = tex_head;
    if (tex_head) tex_head->tprev = b;
    tex_head = b;
}

static void bitmap_drop_tiles(ttm_bitmap* b)
{
    if (!b->tiles) return;
    for (int i = 0; i < b->tcols * b->trows; ++i) gfx_tex_free(b->tiles[i]);
    free(b->tiles);
    b->tiles = NULL;
    tex_unlink(b);
    tex_count--;
    tex_bytes -= b->tbytes;
    b->tbytes = 0;
}

/* give up the textures of bitmaps not drawn in this frame, least recently
 * drawn first, until `want` bytes have been freed; returns the bytes freed.
 * The first pass spares bitmaps whose pixels were shed (no bands): theirs
 * would take a decode to make again, not just an upload. */
static long tex_evict(long want, const ttm_bitmap* keep)
{
    ttm_bitmap* last = tex_head;
    while (last && last->tnext) last = last->tnext;
    long freed = 0;
    for (int pass = 0; pass < 2 && freed < want; ++pass)
        for (ttm_bitmap* b = last; b && freed < want;) {
            ttm_bitmap* prev = b->tprev;
            if (b != keep && b->tframe != ttm_render_frame && (pass || b->bands)) {
                if (b == last) last = prev;
                freed += b->tbytes;
                bitmap_drop_tiles(b);
            }
            b = prev;
        }
    return freed;
}

void ttm_bitmap_tex_stats(long* count, long* bytes, long* failed)
{
    *count = tex_count;
    *bytes = tex_bytes;
    *failed = tex_failed;
}

static long pot_of(int v)
{
    long p = 8;
    while (p < v) p <<= 1;
    return p;
}

/* texture bytes for a w x h bitmap cut on a d px grid, each piece padded to
 * powers of two as the PICA200 needs */
static long padded_bytes(int w, int h, int d)
{
    long across = 0, down = 0;
    for (int x = 0; x < w; x += d) across += pot_of(w - x < d ? w - x : d);
    for (int y = 0; y < h; y += d) down += pot_of(h - y < d ? h - y : d);
    return across * down * 4;
}

/* The grid a bitmap is cut on: one texture where there is no limit; else
 * whichever of limit, limit/2, limit/4 (not under 256) pads least, the
 * smallest on a tie. Padding is what fills texture memory: 640x1500 on a 1024
 * grid takes 6 MB for 3.8 MB of pixels, on a 512 grid 3.75 MB; a 640x480
 * bitmap 2 MB, or 1.25. And the linear heap fragments like any other (console
 * run 8: a texture not made with 11 MB free), so a 256x6096 tileset is 24
 * textures of 256 KB rather than six of 1 MB, for the same bytes. */
static int grid_for(int w, int h, int limit)
{
    if (limit <= 0) return w > h ? w : h;
    int best = limit;
    long least = padded_bytes(w, h, limit);
    for (int d = limit / 2; d >= 256 && d >= limit / 4; d /= 2) {
        long bytes = padded_bytes(w, h, d);
        if (bytes <= least) { least = bytes; best = d; }    /* a tie: smaller textures fit a fragmented heap */
    }
    if (w <= best && h <= best) return w > h ? w : h;
    return best;
}

/* the textures of b's slices; 0 (and none kept) when any could not be made */
static int bitmap_make_tiles(ttm_bitmap* b)
{
    int d = b->tgrid = grid_for(b->w, b->h, b->tdim);
    b->tcols = (b->w + d - 1) / d;
    b->trows = (b->h + d - 1) / d;
    int n = b->tcols * b->trows;
    b->tiles = (gfx_tex**)calloc((size_t)n, sizeof(gfx_tex*));
    long bytes = 0;
    int ok = 1;
    for (int r = 0; r < b->trows; ++r)
        for (int c = 0; c < b->tcols; ++c) {
            int w = b->w - c * d < d ? b->w - c * d : d;
            int h = b->h - r * d < d ? b->h - r * d : d;
            gfx_tex* t = gfx_tex_new(w, h);
            b->tiles[r * b->tcols + c] = t;
            if (!gfx_tex_ok(t)) ok = 0;
            bytes += gfx_tex_bytes(t);
        }
    if (!ok) {
        for (int i = 0; i < n; ++i) gfx_tex_free(b->tiles[i]);
        free(b->tiles);
        b->tiles = NULL;
        return 0;
    }
    b->tbytes = bytes;
    tex_count++;
    tex_bytes += bytes;
    tex_link_front(b);
    return 1;
}

static long bitmap_tex_need(const ttm_bitmap* b)
{
    return (long)b->w * b->h * 4 * 2;    /* padding can double it */
}

/* ---- decoded pixels are a cache too ---------------------------------------------------
 *
 * RPG::Cache keeps every image the game loads for as long as it runs (so does
 * RGSS; a PC has the RAM), and each bitmap kept its pixels in RAM besides its
 * textures. Console run 9 ran out that way on its first long map: 67 MB free
 * after the title, 9.5 MB by the bedroom, then not even a 510x84 message
 * bitmap could be made. But pixels that are still exactly an image file's
 * (b->file set) can always be decoded again. So those bitmaps are on a list,
 * most recently read first, and when RAM is short the ones not read lately
 * give their pixels up (bands NULL, file kept): first those whose textures
 * are made and current, which draw as before without them. Whatever needs
 * the pixels again -- an upload, a blt from it, get_pixel, the tilemap's
 * autotiles -- decodes the file first (px_need). A bitmap the game drew
 * into is never shed: its pixels exist nowhere else. */

#define RAM_RESERVE_KB (20L * 1024)    /* shed down to this much free */

static ttm_bitmap* res_head;           /* most recently read first */
static long px_total, res_total;       /* bytes: every bitmap's pixels; the sheddable ones */
static long px_shed_n, px_reload_n, px_reload_failed;
static mrb_state* px_mrb;              /* for decoding again (TTM.image_data) */

static long px_bytes(int w, int h) { return (long)w * h * 4; }

static void res_unlink(ttm_bitmap* b)
{
    if (!b->on_res) return;
    if (b->rprev) b->rprev->rnext = b->rnext;
    else res_head = b->rnext;
    if (b->rnext) b->rnext->rprev = b->rprev;
    b->rprev = b->rnext = NULL;
    b->on_res = 0;
    res_total -= px_bytes(b->w, b->h);
}

/* b's pixels were just read (or made): to the front, if they could be shed */
static void px_used(ttm_bitmap* b)
{
    b->pused = ttm_render_frame;
    if (!b->file || !b->bands || b->disposed || res_head == b) return;
    res_unlink(b);
    b->rprev = NULL;
    b->rnext = res_head;
    if (res_head) res_head->rprev = b;
    res_head = b;
    b->on_res = 1;
    res_total += px_bytes(b->w, b->h);
}

static void bitmap_free_px(ttm_bitmap* b);

/* free the pixels of file images, least recently read first, until `want`
 * bytes have gone, never k1's or k2's (what the caller is working on), and
 * only images not read for `idle` frames. Pass 1 takes only bitmaps whose
 * textures are current (nothing to redo while they stay), pass 2 any not
 * read this frame, and when idle is 0 (an allocation waits) pass 3 any. */
static long px_shed(long want, const ttm_bitmap* k1, const ttm_bitmap* k2, uint32_t idle)
{
    long freed = 0;
    for (int pass = 0; pass < (idle ? 2 : 3) && freed < want; ++pass) {
        ttm_bitmap* b = res_head;
        while (b && b->rnext) b = b->rnext;
        while (b && freed < want) {
            ttm_bitmap* prev = b->rprev;
            uint32_t age = ttm_render_frame - b->pused;
            int ok = pass == 2 || (age >= idle && (pass == 1 ? age > 0 : b->tiles && !b->dirty));
            if (b != k1 && b != k2 && ok) {
                freed += px_bytes(b->w, b->h);
                res_unlink(b);
                bitmap_free_px(b);
                px_shed_n++;
            }
            b = prev;
        }
    }
    return freed;
}

/* $ttm_ram_budget_kb (PC only: no game script sets it) pretends the pixels
 * have that much RAM, so shedding runs off the console (pc/shed_check.rb) */
static long ram_budget(void)
{
    if (!px_mrb) return 0;
    mrb_value v = mrb_gv_get(px_mrb, mrb_intern_lit(px_mrb, "$ttm_ram_budget_kb"));
    return mrb_integer_p(v) && mrb_integer(v) > 0 ? (long)mrb_integer(v) * 1024 : 0;
}

/* shed what `need` more bytes would take from the reserve; free_kb < 0: read it */
static void px_make_room(long need, long free_kb, const ttm_bitmap* k1, const ttm_bitmap* k2, uint32_t idle)
{
    long budget = ram_budget(), deficit;
    if (budget > 0) {
        deficit = px_total + need - budget;
    } else {
        if (free_kb < 0) free_kb = gfx_mem_free_kb();
        if (free_kb <= 0) return;    /* the platform cannot say (the PC) */
        deficit = (need / 1024 + RAM_RESERVE_KB - free_kb) * 1024;
    }
    if (deficit > 0) px_shed(deficit, k1, k2, idle);
}

/* between frames nothing waits, so only images left unread for a second or
 * more: one the game blts from every few frames would be decoded again and
 * again */
void ttm_bitmap_relieve(long free_kb) { px_make_room(0, free_kb, NULL, NULL, 40); }

void ttm_bitmap_ram_stats(long* bytes, long* sheddable, long* shed, long* reloaded, long* failed)
{
    *bytes = px_total;
    *sheddable = res_total;
    *shed = px_shed_n;
    *reloaded = px_reload_n;
    *failed = px_reload_failed;
}

void ttm_bitmap_ram_note(char* out, size_t n)
{
    long free_kb = gfx_mem_free_kb(), big = free_kb > 0 ? gfx_mem_largest_kb() : 0;
    if (free_kb > 0)
        snprintf(out, n, "%ld KB free, largest %ld KB, %ld KB px (%ld KB sheddable)", free_kb, big,
                 px_total / 1024, res_total / 1024);
    else
        snprintf(out, n, "%ld KB px (%ld KB sheddable)", px_total / 1024, res_total / 1024);
}

static int decode_into(ttm_bitmap* b, const unsigned char* raw, int len, int png_by_stb, const char** why);
static int use_stb_for_png(mrb_state* mrb);

static mrb_value image_data_of(mrb_state* mrb, void* file)
{
    return mrb_funcall(mrb, mrb_obj_value(mrb_module_get(mrb, "TTM")), "image_data", 1,
                       mrb_str_new_cstr(mrb, (const char*)file));
}

/* b's pixels, decoding its file again if they were shed; never sheds `keep`.
 * This runs while a frame is drawn too, so no collection may run (the frame's
 * drawables are plain pointers) and nothing may raise. */
static int px_need(ttm_bitmap* b, const ttm_bitmap* keep)
{
    if (b->bands) {
        px_used(b);
        return 1;
    }
    if (!b->file || b->disposed || !px_mrb) return 0;
    mrb_state* mrb = px_mrb;
    px_make_room(px_bytes(b->w, b->h), -1, b, keep, 0);
    int ai = mrb_gc_arena_save(mrb);
    mrb_bool gc_was = mrb->gc.disabled;
    mrb->gc.disabled = TRUE;
    mrb_bool err = FALSE;
    mrb_value data = mrb_protect_error(mrb, image_data_of, b->file, &err);
    if (err) mrb->exc = NULL;
    ttm_bitmap t;
    memset(&t, 0, sizeof(t));
    int ok = 0;
    if (!err && mrb_string_p(data)) {
        const char* why = NULL;
        int stb = use_stb_for_png(mrb);
        ok = decode_into(&t, (const unsigned char*)RSTRING_PTR(data), (int)RSTRING_LEN(data), stb, &why);
        if (!ok) {
            ttm_bitmap_unkeep_all();
            px_shed(LONG_MAX, b, keep, 0);
            ok = decode_into(&t, (const unsigned char*)RSTRING_PTR(data), (int)RSTRING_LEN(data), stb, &why);
        }
    }
    mrb->gc.disabled = gc_was;
    mrb_gc_arena_restore(mrb, ai);
    if (ok && (t.w != b->w || t.h != b->h)) {    /* not the image it was: leave it blank */
        ttm_bitmap_free_px(&t);
        ok = 0;
    }
    if (!ok) {
        px_reload_failed++;
        return 0;
    }
    b->bands = t.bands;
    b->nbands = t.nbands;
    b->bshift = t.bshift;
    px_reload_n++;
    px_used(b);
    return 1;
}

int ttm_bitmap_pixels(ttm_bitmap* b) { return b && px_need(b, NULL); }

/* ---- kept images: decoded pixels that outlive a dispose ------------------------------
 *
 * Spriteset_Map#dispose disposes the tileset and the autotiles on every map
 * change, and the next map's spriteset asks RPG::Cache for them again -- the
 * same files, as often as not. A decode that failed there left the map with
 * only its autotiles (console run 7), and even when it works it costs a
 * tileset's worth of inflating. So a dispose of a large bitmap whose pixels
 * are still exactly its file's keeps them, and Bitmap.new of that file takes
 * them back instead of decoding. Kept images are freed before a large decode
 * of another file when RAM is short, when RAM runs low (graphics.c) and
 * before a decode is retried. Only explicit disposes keep: what the collector
 * frees is freed.
 *
 * Not only tilesets: Sprite_Picture#dispose disposes its picture, which is
 * RPG::Cache's own bitmap, so every map change threw the game's pictures away
 * and the next map read and decoded them again -- a third of a map change
 * (pc/mapload_profile.rb). Any file image from 16 KB up is kept now. */

#define KEEP_SLOTS 32
#define KEEP_MIN_BYTES (16L * 1024)          /* smaller images decode in no time */
#define KEEP_MAX_BYTES (16L * 1024 * 1024)   /* all kept images together */

typedef struct {
    char* file;
    uint32_t** bands;
    int nbands, bshift, w, h;
    uint32_t seq;        /* when kept: the oldest goes first */
} kept_image;

static kept_image kept[KEEP_SLOTS];
static long kept_bytes, kept_reused;
static uint32_t kept_seq;

static void bands_free(uint32_t** bands, int n)
{
    if (!bands) return;
    for (int i = 0; i < n; ++i) free(bands[i]);
    free(bands);
}

int ttm_bitmap_alloc(ttm_bitmap* b, int w, int h)
{
    int shift = 0;    /* rows per band: the most, as a power of two, within TTM_BAND_BYTES */
    while (shift < 30 && ((size_t)2 << shift) * (size_t)w * 4 <= TTM_BAND_BYTES) shift++;
    int rows = 1 << shift, n = (h + rows - 1) >> shift;
    uint32_t** bands = (uint32_t**)calloc((size_t)(n > 0 ? n : 1), sizeof(uint32_t*));
    if (!bands) return 0;
    for (int i = 0; i < n; ++i) {
        int r = h - i * rows < rows ? h - i * rows : rows;
        bands[i] = (uint32_t*)calloc((size_t)r * (size_t)w, 4);
        if (!bands[i]) {
            bands_free(bands, i);
            return 0;
        }
    }
    b->w = w;
    b->h = h;
    b->bands = bands;
    b->nbands = n;
    b->bshift = shift;
    px_total += px_bytes(w, h);
    return 1;
}

static void bitmap_free_px(ttm_bitmap* b)
{
    if (!b->bands) return;
    res_unlink(b);
    bands_free(b->bands, b->nbands);
    px_total -= px_bytes(b->w, b->h);
    b->bands = NULL;
    b->nbands = 0;
}

void ttm_bitmap_free_px(ttm_bitmap* b) { bitmap_free_px(b); }

static void kept_free(kept_image* k)
{
    free(k->file);
    bands_free(k->bands, k->nbands);
    kept_bytes -= px_bytes(k->w, k->h);
    memset(k, 0, sizeof(*k));
}

void ttm_bitmap_unkeep_all(void)
{
    for (int i = 0; i < KEEP_SLOTS; ++i)
        if (kept[i].bands) kept_free(&kept[i]);
}

/* b is being disposed: keep its pixels if they are a large file's, untouched;
 * else free them. Either way b is left without px and file. */
static void keep_or_free(ttm_bitmap* b)
{
    long bytes = px_bytes(b->w, b->h);
    res_unlink(b);
    if (b->file && b->bands && bytes >= KEEP_MIN_BYTES && bytes <= KEEP_MAX_BYTES) {
        for (int i = 0; i < KEEP_SLOTS; ++i)
            if (kept[i].bands && strcmp(kept[i].file, b->file) == 0) kept_free(&kept[i]);
        for (;;) {
            kept_image* oldest = NULL;
            int free_slot = 0;
            for (int i = 0; i < KEEP_SLOTS; ++i) {
                if (!kept[i].bands) free_slot = 1;
                else if (!oldest || kept[i].seq < oldest->seq) oldest = &kept[i];
            }
            if (free_slot && kept_bytes + bytes <= KEEP_MAX_BYTES) break;
            kept_free(oldest);
        }
        for (int i = 0; i < KEEP_SLOTS; ++i)
            if (!kept[i].bands) {
                kept[i] = (kept_image){ b->file, b->bands, b->nbands, b->bshift, b->w, b->h, ++kept_seq };
                kept_bytes += bytes;
                px_total -= bytes;    /* a kept image belongs to no bitmap */
                b->file = NULL;
                b->bands = NULL;
                b->nbands = 0;
                return;
            }
    }
    bitmap_free_px(b);
    free(b->file);
    b->file = NULL;
}

/* Bitmap.new(file): its kept pixels, if any -- 1 when b now has them */
static int unkeep(ttm_bitmap* b, const char* file)
{
    for (int i = 0; i < KEEP_SLOTS; ++i) {
        kept_image* k = &kept[i];
        if (!k->bands || strcmp(k->file, file) != 0) continue;
        b->bands = k->bands;
        b->nbands = k->nbands;
        b->bshift = k->bshift;
        b->w = k->w;
        b->h = k->h;
        b->file = k->file;
        kept_bytes -= px_bytes(k->w, k->h);
        px_total += px_bytes(k->w, k->h);
        memset(k, 0, sizeof(*k));
        kept_reused++;
        return 1;
    }
    return 0;
}

/* pixels are about to change: they are no longer the file's (and so can
 * never be shed) */
static void bitmap_touch(ttm_bitmap* b)
{
    b->dirty = 1;
    if (b->file) {
        res_unlink(b);
        free(b->file);
        b->file = NULL;
    }
}

static void bitmap_free(mrb_state* mrb, void* p)
{
    ttm_bitmap* b = (ttm_bitmap*)p;
    if (!b) return;
    res_unlink(b);
    bitmap_free_px(b);
    free(b->file);
    bitmap_drop_tiles(b);
    free(b);
}

const mrb_data_type ttm_bitmap_type = { "Bitmap", bitmap_free };
static uint32_t bitmap_serials;

ttm_bitmap* ttm_bitmap_new_raw(int w, int h)
{
    ttm_bitmap* b = (ttm_bitmap*)calloc(1, sizeof(ttm_bitmap));
    b->serial = ++bitmap_serials;
    b->tdim = gfx_max_tex_dim();
    b->dirty = 1;
    if (!ttm_bitmap_alloc(b, w, h)) {
        free(b);
        return NULL;
    }
    return b;
}

void ttm_bitmap_destroy(ttm_bitmap* b) { bitmap_free(NULL, b); }

ttm_bitmap* ttm_bitmap_ptr(mrb_value v)
{
    if (mrb_data_p(v) && DATA_TYPE(v) == &ttm_bitmap_type) {
        ttm_bitmap* b = (ttm_bitmap*)DATA_PTR(v);
        if (b && !b->disposed) return b;
    }
    return NULL;
}

/* the backend copy: a grid of slices (grid_for: one when the bitmap fits and
 * that pads least), made on first draw and refilled when px changes.
 * 0 when there is no texture memory for it this frame. */
static int bitmap_upload(ttm_bitmap* b)
{
    if (!b->tiles) {
        if (!bitmap_make_tiles(b)) {
            tex_evict(bitmap_tex_need(b), b);
            if (!bitmap_make_tiles(b)) {
                tex_failed++;
                return 0;
            }
        }
        b->dirty = 1;
    } else if (b != tex_head) {    /* most recently drawn first */
        tex_unlink(b);
        tex_link_front(b);
    }
    b->tframe = ttm_render_frame;
    if (b->dirty) {
        px_used(b);
        int d = b->tgrid;
        for (int r = 0; r < b->trows; ++r)
            for (int c = 0; c < b->tcols; ++c)
                gfx_tex_upload(b->tiles[r * b->tcols + c], b, c * d, r * d);
        b->dirty = 0;
    }
    return 1;
}

void ttm_bitmap_draw(ttm_bitmap* b, gfx_q q, int alpha, int blend, const uint8_t tint[4])
{
    if (!b || q.sw <= 0 || q.sh <= 0) return;
    /* shed pixels: current textures draw without them; else decode again */
    if (!b->bands && (!b->tiles || b->dirty) && !px_need(b, NULL)) return;
    if (!bitmap_upload(b)) return;
    if (b->tcols == 1 && b->trows == 1) {
        ttm_draw(b->tiles[0], q, alpha, blend, tint);
        return;
    }
    /* one quad per slice the texture rect touches: its part of the rect, placed
     * at that part's offset inside q (scaled, then rotated about q's top-left
     * as the backend rotates q; mirrored, the offset runs from the right) */
    int d = b->tgrid;
    float kx = q.w / q.sw, ky = q.h / q.sh;
    float ca = cosf(q.angle), sa = sinf(q.angle);
    float sx1 = q.sx + q.sw, sy1 = q.sy + q.sh;
    int c0 = (int)(q.sx / d), r0 = (int)(q.sy / d);
    for (int r = r0; r < b->trows && r * d < sy1; ++r) {
        float y0 = q.sy > r * d ? q.sy : (float)(r * d);
        float y1 = sy1 < (r + 1) * d ? sy1 : (float)((r + 1) * d);
        if (y1 <= y0) continue;
        for (int c = c0; c < b->tcols && c * d < sx1; ++c) {
            float x0 = q.sx > c * d ? q.sx : (float)(c * d);
            float x1 = sx1 < (c + 1) * d ? sx1 : (float)((c + 1) * d);
            if (x1 <= x0) continue;
            gfx_q p = q;
            p.sx = x0 - c * d; p.sy = y0 - r * d;
            p.sw = x1 - x0;    p.sh = y1 - y0;
            float lx = (q.mirror ? sx1 - x1 : x0 - q.sx) * kx;
            float ly = (y0 - q.sy) * ky;
            p.x = q.x + lx * ca + ly * sa;
            p.y = q.y - lx * sa + ly * ca;
            p.w = p.sw * kx;
            p.h = p.sh * ky;
            ttm_draw(b->tiles[r * b->tcols + c], p, alpha, blend, tint);
        }
    }
}

static ttm_bitmap* get(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = (ttm_bitmap*)mrb_data_get_ptr(mrb, self, &ttm_bitmap_type);
    if (!b || b->disposed) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "disposed bitmap");
    return b;
}

/* get(), for a method that reads or writes the pixels: they are decoded
 * again first if they were shed (never shedding `keep`, the other bitmap of a
 * blt) */
static ttm_bitmap* get_px(mrb_state* mrb, mrb_value self, const ttm_bitmap* keep)
{
    ttm_bitmap* b = get(mrb, self);
    if (!px_need(b, keep)) {
        char note[128];
        ttm_bitmap_ram_note(note, sizeof(note));
        mrb_raisef(mrb, mrb_class_get(mrb, "RGSSError"), "cannot decode image %s again: out of memory (%s)",
                   b->file ? b->file : "?", note);
    }
    return b;
}

/* ---- pixel helpers -------------------------------------------------------------- */

#define R(p) ((p) & 0xff)
#define G(p) (((p) >> 8) & 0xff)
#define B(p) (((p) >> 16) & 0xff)
#define A(p) ((p) >> 24)
#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

/* src over dst, src alpha scaled by opacity (0..255) */
static inline void blend_over(uint32_t* d, uint32_t s, int opacity)
{
    int sa = (int)A(s) * opacity / 255;
    if (sa <= 0) return;
    if (sa >= 255) {
        *d = (s & 0x00ffffff) | 0xff000000u;
        return;
    }
    int da = (int)A(*d);
    int oa = sa + da * (255 - sa) / 255;
    if (oa <= 0) {
        *d = 0;
        return;
    }
    int dw = da * (255 - sa) / 255;
    int r = ((int)R(s) * sa + (int)R(*d) * dw) / oa;
    int g = ((int)G(s) * sa + (int)G(*d) * dw) / oa;
    int bb = ((int)B(s) * sa + (int)B(*d) * dw) / oa;
    *d = RGBA(r, g, bb, oa);
}

void ttm_color_bytes(mrb_value color, uint8_t out[4])
{
    ttm_quad_t* q = ttm_color_ptr(color);
    for (int i = 0; i < 4; ++i) out[i] = q ? (uint8_t)(q->v[i] + 0.5) : 0;
}

/* ---- construction ----------------------------------------------------------------- */

static mrb_value new_font(mrb_state* mrb)
{
    return mrb_funcall(mrb, mrb_obj_value(mrb_class_get(mrb, "Font")), "new", 0);
}

/* ---- large and failed images: the log ------------------------------------------ */

/* The engine's log: ttm_log (Kernel, rgss/compat.rb) is TTM.log on the 3DS --
 * console and log.txt alike -- and print on the PC. printf until it is loaded. */
static void bitmap_log(mrb_state* mrb, const char* msg)
{
    mrb_value main = mrb_top_self(mrb);
    if (mrb_respond_to(mrb, main, mrb_intern_lit(mrb, "ttm_log")))
        mrb_funcall(mrb, main, "ttm_log", 1, mrb_str_new_cstr(mrb, msg));
    else
        printf("%s\n", msg);
}

/* One line per distinct large image: a map change disposes its tileset and the
 * next map loads it again, so a tileset would fill the log with itself. A
 * failure is logged every time (`always`): a failed reload of an image that
 * loaded fine before used to be silent, the map simply losing its tiles. */
static char noted_names[16][64];
static int noted_n, noted_more;

static void bitmap_note(mrb_state* mrb, mrb_value name, int w, int h, const char* why, int always)
{
    char key[64], msg[384];
    snprintf(key, sizeof(key), "%s", RSTRING_PTR(name));
    if (!always) {
        for (int i = 0; i < noted_n; ++i)
            if (strcmp(noted_names[i], key) == 0) return;
        if (noted_n == 16) {
            if (!noted_more++) bitmap_log(mrb, "gfx: more large images (not listed)");
            return;
        }
        snprintf(noted_names[noted_n++], sizeof(noted_names[0]), "%s", key);
    }
    /* free RAM and texture memory, where the platform can say (the 3DS) */
    char mem[176] = "";
    if (always) {    /* a failure: the whole picture of RAM, largest free block too */
        mem[0] = ',';
        mem[1] = ' ';
        ttm_bitmap_ram_note(mem + 2, sizeof(mem) - 2);
    } else if (gfx_mem_free_kb() > 0) {
        snprintf(mem, sizeof(mem), ", %ld KB free", gfx_mem_free_kb());
    }
    if (gfx_tex_mem_free_kb() > 0)
        snprintf(mem + strlen(mem), sizeof(mem) - strlen(mem), ", %ld KB tex", gfx_tex_mem_free_kb());
    if (w > 0 && h > 0)
        snprintf(msg, sizeof(msg), "gfx: %s is %dx%d: %s%s", key, w, h, why, mem);
    else
        snprintf(msg, sizeof(msg), "gfx: %s: %s%s", key, why, mem);
    bitmap_log(mrb, msg);
}

/* $ttm_strict_assets = true makes these failures fatal again, as they were
 * before the M3 hardware run -- for chasing an asset that should work. */
static int bitmap_strict(mrb_state* mrb)
{
    return mrb_test(mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_strict_assets")));
}

/* The slice size: the backend's texture limit, overridable with
 * $ttm_max_tex_dim (0 = one texture whatever the size). That is how slicing is
 * exercised on the PC (pc/slice_check.rb), whose software renderer has no
 * limit of its own. No game script sets either global. */
static int bitmap_max_dim(mrb_state* mrb, int backend)
{
    mrb_value v = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_max_tex_dim"));
    if (mrb_integer_p(v)) {
        int n = (int)mrb_integer(v);
        if (n >= 0) return n;
    }
    return backend;
}

/* RGSS's stand-in for an image that is not there: a blank 32x32 bitmap, so the
 * game keeps running and nothing is drawn where the image would have been. */
static void bitmap_blank(ttm_bitmap* b)
{
    ttm_bitmap_alloc(b, 32, 32);
}

/* $ttm_png_stb = true decodes PNGs through stb as before png.c, so
 * pc/png_check.rb can compare the two; no game script sets it */
static int use_stb_for_png(mrb_state* mrb)
{
    return mrb_test(mrb_gv_get(mrb, mrb_intern_lit(mrb, "$ttm_png_stb")));
}

/* raw (an image file's bytes) into b's bands: a PNG row by row (png.c), else
 * (JPEG, BMP) through stb, then copied into bands. 1 decoded; 0 failed, *why
 * says why. */
static long decoded_by_png, decoded_by_stb;

static int decode_into(ttm_bitmap* b, const unsigned char* raw, int len, int png_by_stb, const char** why)
{
    if (!png_by_stb) {
        int r = ttm_png_decode(raw, (size_t)len, b, why);
        if (r > 0) decoded_by_png++;
        if (r != 0) return r > 0;
    }
    decoded_by_stb++;
    /* a JPEG has no alpha: decoded as RGB it takes 3/4 of the RAM */
    int jpeg = len > 2 && raw[0] == 0xff && raw[1] == 0xd8;
    int req = jpeg ? 3 : 4, w = 0, h = 0, n = 0;
    ttm_stbi_clear_failure();
    unsigned char* px = stbi_load_from_memory(raw, len, &w, &h, &n, req);
    if (!px) {
        *why = stbi_failure_reason();
        if (!*why) *why = "no reason given";
        return 0;
    }
    if (!ttm_bitmap_alloc(b, w, h)) {
        stbi_image_free(px);
        *why = "outofmem";
        return 0;
    }
    for (int y = 0; y < h; ++y) {
        uint32_t* d = ttm_row(b, y);
        const unsigned char* src = px + (size_t)y * w * req;
        if (req == 4)
            memcpy(d, src, (size_t)w * 4);
        else
            for (int x = 0; x < w; ++x, src += 3) d[x] = RGBA(src[0], src[1], src[2], 255);
    }
    stbi_image_free(px);
    return 1;
}

/* w x h pixels for b, freeing what can be freed when RAM is short: other
 * images' pixels (never keep's) to the reserve; on a failure kept images, a
 * collection and more pixels; at last every image's. 0: none to be had. */
static int alloc_hard(mrb_state* mrb, ttm_bitmap* b, int w, int h, const ttm_bitmap* keep)
{
    px_make_room(px_bytes(w, h), -1, keep, NULL, 0);
    if (ttm_bitmap_alloc(b, w, h)) return 1;
    ttm_bitmap_unkeep_all();
    px_shed(px_bytes(w, h) + RAM_RESERVE_KB * 1024, keep, NULL, 0);
    mrb_full_gc(mrb);
    if (ttm_bitmap_alloc(b, w, h)) return 1;
    px_shed(LONG_MAX, keep, NULL, 0);
    return ttm_bitmap_alloc(b, w, h);
}

static mrb_value bitmap_init(mrb_state* mrb, mrb_value self)
{
    mrb_value a1, a2 = mrb_nil_value();
    mrb_int argc = mrb_get_args(mrb, "o|o", &a1, &a2);

    ttm_bitmap* old = (ttm_bitmap*)DATA_PTR(self);
    if (old) bitmap_free(mrb, old);
    mrb_data_init(self, NULL, &ttm_bitmap_type);

    ttm_bitmap* b = (ttm_bitmap*)calloc(1, sizeof(ttm_bitmap));
    b->serial = ++bitmap_serials;
    b->tdim = bitmap_max_dim(mrb, gfx_max_tex_dim());
    if (argc >= 2) {
        mrb_int w = ttm_to_int(mrb, a1), h = ttm_to_int(mrb, a2);
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
            free(b);
            mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "failed to create bitmap");
        }
        if (!alloc_hard(mrb, b, (int)w, (int)h, NULL)) {
            free(b);
            char note[128];
            ttm_bitmap_ram_note(note, sizeof(note));
            mrb_raisef(mrb, mrb_class_get(mrb, "RGSSError"), "failed to create bitmap %d x %d: out of memory (%s)",
                       (int)w, (int)h, note);
        }
    } else {
        mrb_value name = mrb_obj_as_string(mrb, a1);
        const char* file = mrb_string_value_cstr(mrb, &name);
        if (unkeep(b, file)) {    /* disposed earlier, pixels kept: no decode */
            if (kept_reused == 1) {
                char msg[160];
                snprintf(msg, sizeof(msg), "gfx: %s reused after a dispose (decoded once; "
                         "later reuses are not logged)", file);
                bitmap_log(mrb, msg);
            }
            b->dirty = 1;
            px_used(b);
            mrb_data_init(self, b, &ttm_bitmap_type);
            mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@font"), new_font(mrb));
            return self;
        }
        mrb_value data = mrb_funcall(mrb, mrb_obj_value(mrb_module_get(mrb, "TTM")), "image_data", 1, name);
        if (!mrb_string_p(data)) {
            free(b);
            mrb_value enoent = mrb_obj_value(mrb_class_get_under(mrb, mrb_module_get(mrb, "Errno"), "ENOENT"));
            mrb_exc_raise(mrb, mrb_funcall(mrb, enoent, "new", 1, name));
        }
        const unsigned char* raw = (const unsigned char*)RSTRING_PTR(data);
        int len = (int)RSTRING_LEN(data);
        int png_by_stb = use_stb_for_png(mrb);
        int iw = 0, ih = 0, ic = 0;
        int png = ttm_png_info(raw, (size_t)len, &iw, &ih);
        int known = png || stbi_info_from_memory(raw, len, &iw, &ih, &ic);
        /* The collector cannot see the RAM that dead bitmaps still hold (a
         * map change decodes its tileset again while the old copy waits to be
         * collected): collect before a big decode when RAM is short, and once
         * more after a decode that failed (console run 4). A PNG needs only
         * its pixels; stb (JPEG) about twice that. */
        long need_kb = known ? (long)iw * ih * 4 * (png && !png_by_stb ? 1 : 2) / 1024 : 0;
        long free_kb = gfx_mem_free_kb();
        if (need_kb > 1024 && free_kb > 0 && free_kb < need_kb + 16 * 1024) {
            ttm_bitmap_unkeep_all();    /* not this file's: their RAM is worth more now */
            mrb_full_gc(mrb);
            free_kb = -1;
        }
        px_make_room(need_kb * 1024, free_kb, NULL, NULL, 0);    /* other images' pixels, if RAM is short */
        const char* first_why = NULL;
        int ok = decode_into(b, raw, len, png_by_stb, &first_why);
        const char* why = first_why;
        if (!ok && known) {
            /* once more, after a collection and from a fresh read of the
             * archive (in case the bytes, not the memory, were the problem) */
            ttm_bitmap_unkeep_all();
            px_shed(need_kb * 1024 + RAM_RESERVE_KB * 1024, NULL, NULL, 0);
            mrb_full_gc(mrb);
            mrb_value again = mrb_funcall(mrb, mrb_obj_value(mrb_module_get(mrb, "TTM")), "image_data", 1, name);
            if (mrb_string_p(again)) {
                data = again;
                raw = (const unsigned char*)RSTRING_PTR(data);
                len = (int)RSTRING_LEN(data);
            }
            ok = decode_into(b, raw, len, png_by_stb, &why);
        }
        if (ok) {
            int w = b->w, h = b->h;
            b->file = (char*)malloc(strlen(file) + 1);
            if (b->file) strcpy(b->file, file);
            px_used(b);
            if (b->tdim > 0 && (w > b->tdim || h > b->tdim)) {
                /* drawn as slices (bitmap_upload): worth one line, with the
                 * memory left, since these are the biggest images there are */
                int d = grid_for(w, h, b->tdim);
                char note[64];
                snprintf(note, sizeof(note), "%d slices of %d px, %ld KB as textures",
                         ((w + d - 1) / d) * ((h + d - 1) / d), d, padded_bytes(w, h, d) / 1024);
                bitmap_note(mrb, name, w, h, note, 0);
            }
        } else {
            if (!why) why = "no reason given";
            if (!bitmap_strict(mrb) && (known || !strcmp(why, "outofmem") || !strcmp(why, "too large"))) {
                /* Out of RAM (or an image too big to decode), not a bad image:
                 * its header read fine. Log both attempts' reasons and the
                 * bytes it starts with, and carry on with a blank bitmap. */
                char note[96];
                snprintf(note, sizeof(note), "decode failed twice (%s / %s; %d bytes, %02x %02x %02x %02x)",
                         first_why ? first_why : "no reason given", why, len,
                         len > 0 ? raw[0] : 0, len > 1 ? raw[1] : 0, len > 2 ? raw[2] : 0, len > 3 ? raw[3] : 0);
                bitmap_note(mrb, name, known ? iw : 0, known ? ih : 0, note, 1);
                bitmap_blank(b);
            } else {
                free(b);
                char head[32] = "";
                for (int i = 0; i < 8 && i < len; ++i)
                    snprintf(head + i * 3, 4, "%02x ", raw[i]);
                mrb_raisef(mrb, mrb_class_get(mrb, "RGSSError"), "cannot decode image %v: %s (%d bytes: %s)",
                           name, why, len, head);
            }
        }
    }
    b->dirty = 1;
    mrb_data_init(self, b, &ttm_bitmap_type);
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@font"), new_font(mrb));
    return self;
}

static mrb_value bitmap_copy(mrb_state* mrb, mrb_value self)
{
    mrb_value src;
    mrb_get_args(mrb, "o", &src);
    ttm_bitmap* s = get_px(mrb, src, NULL);
    ttm_bitmap* old = (ttm_bitmap*)DATA_PTR(self);
    if (old) bitmap_free(mrb, old);
    mrb_data_init(self, NULL, &ttm_bitmap_type);
    ttm_bitmap* b = (ttm_bitmap*)calloc(1, sizeof(ttm_bitmap));
    b->serial = ++bitmap_serials;
    b->tdim = bitmap_max_dim(mrb, gfx_max_tex_dim());
    if (!alloc_hard(mrb, b, s->w, s->h, s)) {
        free(b);
        mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "failed to copy bitmap: out of memory");
    }
    for (int y = 0; y < s->h; ++y) memcpy(ttm_row(b, y), ttm_row(s, y), (size_t)s->w * 4);
    b->dirty = 1;
    if (s->file && (b->file = (char*)malloc(strlen(s->file) + 1))) {    /* still the file's pixels */
        strcpy(b->file, s->file);
        px_used(b);
    }
    mrb_data_init(self, b, &ttm_bitmap_type);
    mrb_value font = mrb_iv_get(mrb, src, mrb_intern_lit(mrb, "@font"));
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@font"), mrb_nil_p(font) ? new_font(mrb) : mrb_obj_dup(mrb, font));
    return self;
}

/* ---- simple accessors ------------------------------------------------------------------ */

static mrb_value bitmap_dispose(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = (ttm_bitmap*)DATA_PTR(self);
    if (b && !b->disposed) {
        b->disposed = 1;
        keep_or_free(b);
        bitmap_drop_tiles(b);
    }
    return mrb_nil_value();
}

static mrb_value bitmap_disposed(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = (ttm_bitmap*)DATA_PTR(self);
    return mrb_bool_value(!b || b->disposed);
}

static mrb_value bitmap_width(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, get(mrb, self)->w); }
static mrb_value bitmap_height(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, get(mrb, self)->h); }
static mrb_value bitmap_rect(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = get(mrb, self);
    return ttm_rect_new(mrb, 0, 0, b->w, b->h);
}

/* ---- blits ------------------------------------------------------------------------- */

static mrb_value bitmap_blt(mrb_state* mrb, mrb_value self)
{
    mrb_value vx, vy, vsrc, vrect;
    mrb_int opacity = 255;
    mrb_get_args(mrb, "oooo|i", &vx, &vy, &vsrc, &vrect, &opacity);
    ttm_bitmap* d = get_px(mrb, self, NULL);
    ttm_bitmap* s = get_px(mrb, vsrc, d);
    ttm_rect_t* r = ttm_rect_ptr(vrect);
    if (!r) mrb_raise(mrb, E_TYPE_ERROR, "blt: src_rect must be a Rect");
    if (opacity <= 0) return self;
    if (opacity > 255) opacity = 255;
    int dx = (int)ttm_to_int(mrb, vx), dy = (int)ttm_to_int(mrb, vy);
    int sx = (int)r->x, sy = (int)r->y, w = (int)r->w, h = (int)r->h;
    /* clip to the source, then to the destination */
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (dx + w > d->w) w = d->w - dx;
    if (dy + h > d->h) h = d->h - dy;
    if (w <= 0 || h <= 0) return self;
    for (int j = 0; j < h; ++j) {
        uint32_t* drow = ttm_row(d, dy + j) + dx;
        const uint32_t* srow = ttm_row(s, sy + j) + sx;
        for (int i = 0; i < w; ++i) blend_over(&drow[i], srow[i], (int)opacity);
    }
    bitmap_touch(d);
    return self;
}

static mrb_value bitmap_stretch_blt(mrb_state* mrb, mrb_value self)
{
    mrb_value vdst, vsrc, vrect;
    mrb_int opacity = 255;
    mrb_get_args(mrb, "ooo|i", &vdst, &vsrc, &vrect, &opacity);
    ttm_bitmap* d = get_px(mrb, self, NULL);
    ttm_bitmap* s = get_px(mrb, vsrc, d);
    ttm_rect_t* dr = ttm_rect_ptr(vdst);
    ttm_rect_t* sr = ttm_rect_ptr(vrect);
    if (!dr || !sr) mrb_raise(mrb, E_TYPE_ERROR, "stretch_blt: rects must be Rects");
    if (opacity <= 0 || dr->w <= 0 || dr->h <= 0 || sr->w <= 0 || sr->h <= 0) return self;
    if (opacity > 255) opacity = 255;
    for (mrb_int j = 0; j < dr->h; ++j) {
        mrb_int y = dr->y + j;
        if (y < 0 || y >= d->h) continue;
        mrb_int syy = sr->y + j * sr->h / dr->h;
        if (syy < 0 || syy >= s->h) continue;
        uint32_t* drow = ttm_row(d, (int)y);
        const uint32_t* srow = ttm_row(s, (int)syy);
        for (mrb_int i = 0; i < dr->w; ++i) {
            mrb_int x = dr->x + i;
            if (x < 0 || x >= d->w) continue;
            mrb_int sxx = sr->x + i * sr->w / dr->w;
            if (sxx < 0 || sxx >= s->w) continue;
            blend_over(&drow[x], srow[sxx], (int)opacity);
        }
    }
    bitmap_touch(d);
    return self;
}

/* (x, y, w, h, ...) or (rect, ...): the rect's four ints and how many args it used */
static int rect_args(mrb_state* mrb, const mrb_value* argv, mrb_int argc, mrb_int out[4])
{
    ttm_rect_t* r = argc >= 1 ? ttm_rect_ptr(argv[0]) : NULL;
    if (r) {
        out[0] = r->x; out[1] = r->y; out[2] = r->w; out[3] = r->h;
        return 1;
    }
    if (argc < 4) mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
    for (int i = 0; i < 4; ++i) out[i] = ttm_to_int(mrb, argv[i]);
    return 4;
}

static mrb_value bitmap_fill_rect(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    ttm_bitmap* b = get_px(mrb, self, NULL);
    mrb_int r[4];
    int used = rect_args(mrb, argv, argc, r);
    if (argc <= used) mrb_raise(mrb, E_ARGUMENT_ERROR, "fill_rect: color missing");
    uint8_t c[4];
    ttm_color_bytes(argv[used], c);
    uint32_t px = RGBA(c[0], c[1], c[2], c[3]);
    mrb_int x0 = r[0] < 0 ? 0 : r[0], y0 = r[1] < 0 ? 0 : r[1];
    mrb_int x1 = r[0] + r[2] > b->w ? b->w : r[0] + r[2];
    mrb_int y1 = r[1] + r[3] > b->h ? b->h : r[1] + r[3];
    for (mrb_int y = y0; y < y1; ++y) {
        uint32_t* row = ttm_row(b, (int)y);
        for (mrb_int x = x0; x < x1; ++x) row[x] = px;
    }
    bitmap_touch(b);
    return self;
}

static mrb_value bitmap_clear(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = get_px(mrb, self, NULL);
    for (int y = 0; y < b->h; ++y) memset(ttm_row(b, y), 0, (size_t)b->w * 4);
    bitmap_touch(b);
    return self;
}

static mrb_value bitmap_get_pixel(mrb_state* mrb, mrb_value self)
{
    mrb_int x, y;
    mrb_get_args(mrb, "ii", &x, &y);
    ttm_bitmap* b = get_px(mrb, self, NULL);
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return ttm_color_new(mrb, 0, 0, 0, 0);
    uint32_t p = ttm_row(b, (int)y)[x];
    return ttm_color_new(mrb, R(p), G(p), B(p), A(p));
}

static mrb_value bitmap_set_pixel(mrb_state* mrb, mrb_value self)
{
    mrb_int x, y;
    mrb_value color;
    mrb_get_args(mrb, "iio", &x, &y, &color);
    ttm_bitmap* b = get_px(mrb, self, NULL);
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return self;
    uint8_t c[4];
    ttm_color_bytes(color, c);
    ttm_row(b, (int)y)[x] = RGBA(c[0], c[1], c[2], c[3]);
    bitmap_touch(b);
    return self;
}

/* rotate hue by `hue` degrees, in YIQ space (luma and alpha kept) */
static mrb_value bitmap_hue_change(mrb_state* mrb, mrb_value self)
{
    mrb_value vh;
    mrb_get_args(mrb, "o", &vh);
    ttm_bitmap* b = get_px(mrb, self, NULL);
    double hue = fmod(ttm_to_dbl(mrb, vh), 360.0);
    if (hue == 0) return self;
    double c = cos(hue * 3.14159265358979 / 180.0), s = sin(hue * 3.14159265358979 / 180.0);
    for (int yy = 0; yy < b->h; ++yy)
    for (uint32_t *row = ttm_row(b, yy), *end = row + b->w; row < end; ++row) {
        uint32_t p = *row;
        if (!A(p)) continue;
        double r = R(p), g = G(p), bb = B(p);
        double y = 0.299 * r + 0.587 * g + 0.114 * bb;
        double ii = 0.596 * r - 0.274 * g - 0.322 * bb;
        double q = 0.211 * r - 0.523 * g + 0.312 * bb;
        double i2 = ii * c - q * s, q2 = ii * s + q * c;
        double nr = y + 0.956 * i2 + 0.621 * q2;
        double ng = y - 0.272 * i2 - 0.647 * q2;
        double nb = y - 1.106 * i2 + 1.703 * q2;
        int ir = nr < 0 ? 0 : nr > 255 ? 255 : (int)(nr + 0.5);
        int ig = ng < 0 ? 0 : ng > 255 ? 255 : (int)(ng + 0.5);
        int ib = nb < 0 ? 0 : nb > 255 ? 255 : (int)(nb + 0.5);
        *row = RGBA(ir, ig, ib, A(p));
    }
    bitmap_touch(b);
    return self;
}

/* ---- text ------------------------------------------------------------------------- */

static mrb_value font_iv(mrb_state* mrb, mrb_value self, const char* name)
{
    mrb_value font = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font"));
    if (mrb_nil_p(font)) return mrb_nil_value();
    return mrb_iv_get(mrb, font, mrb_intern_cstr(mrb, name));
}

static int font_size(mrb_state* mrb, mrb_value self)
{
    mrb_value v = font_iv(mrb, self, "@size");
    return mrb_nil_p(v) ? 22 : (int)ttm_to_int(mrb, v);
}

/* the face the bitmap's Font draws in (font.c): its name, or "" for the
 * default face -- a name with no file of its own (Arial, Times New Roman)
 * draws in the default face too */
static const char* font_name(mrb_state* mrb, mrb_value self)
{
    mrb_value v = font_iv(mrb, self, "@name");
    if (!mrb_string_p(v) || RSTRING_LEN(v) == 0) return "";
    return mrb_str_to_cstr(mrb, v);
}

static mrb_value bitmap_draw_text(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    ttm_bitmap* b = get_px(mrb, self, NULL);
    mrb_int r[4];
    int used = rect_args(mrb, argv, argc, r);
    if (argc <= used) mrb_raise(mrb, E_ARGUMENT_ERROR, "draw_text: text missing");
    mrb_value str = mrb_obj_as_string(mrb, argv[used]);
    int align = argc > used + 1 ? (int)ttm_to_int(mrb, argv[used + 1]) : 0;
    uint8_t color[4] = { 255, 255, 255, 255 };
    mrb_value vc = font_iv(mrb, self, "@color");
    if (!mrb_nil_p(vc)) ttm_color_bytes(vc, color);
    mrb_value vbold = font_iv(mrb, self, "@bold");
    ttm_text_draw(b, font_name(mrb, self), (int)r[0], (int)r[1], (int)r[2], (int)r[3],
                  RSTRING_PTR(str), (int)RSTRING_LEN(str),
                  font_size(mrb, self), mrb_test(vbold), color, align);
    bitmap_touch(b);
    return self;
}

static mrb_value bitmap_text_size(mrb_state* mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    get(mrb, self);
    mrb_value str = mrb_obj_as_string(mrb, v);
    int size = font_size(mrb, self);
    const char* face = font_name(mrb, self);
    return ttm_rect_new(mrb, 0, 0,
                        ttm_text_width(face, RSTRING_PTR(str), (int)RSTRING_LEN(str), size),
                        ttm_text_height(face, size));
}

static mrb_value bitmap_font(mrb_state* mrb, mrb_value self)
{
    return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@font"));
}

static mrb_value bitmap_set_font(mrb_state* mrb, mrb_value self)
{
    mrb_value f;
    mrb_get_args(mrb, "o", &f);
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@font"), f);
    return f;
}

/* Bitmap#ttm_checksum: FNV-1a over every pixel, row by row (pc/png_check.rb) */
static mrb_value bitmap_checksum(mrb_state* mrb, mrb_value self)
{
    ttm_bitmap* b = get_px(mrb, self, NULL);
    uint32_t h = 2166136261u;
    for (int y = 0; y < b->h; ++y) {
        const unsigned char* p = (const unsigned char*)ttm_row(b, y);
        for (size_t i = 0; i < (size_t)b->w * 4; ++i) h = (h ^ p[i]) * 16777619u;
    }
    return mrb_int_value(mrb, (mrb_int)h);
}

/* Bitmap#ttm_resident?: whether its pixels are in RAM now (pc/shed_check.rb) */
static mrb_value bitmap_resident(mrb_state* mrb, mrb_value self)
{
    return mrb_bool_value(get(mrb, self)->bands != NULL);
}

/* Bitmap.ttm_decoded: [images decoded by png.c, by stb] */
static mrb_value bitmap_s_decoded(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, decoded_by_png));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, decoded_by_stb));
    return a;
}

/* Bitmap.ttm_kept: [images reused instead of decoded, images kept now, their
 * bytes] (pc/keep_check.rb) */
static mrb_value bitmap_s_kept(mrb_state* mrb, mrb_value self)
{
    long n = 0;
    for (int i = 0; i < KEEP_SLOTS; ++i) n += kept[i].bands != NULL;
    mrb_value a = mrb_ary_new_capa(mrb, 3);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, kept_reused));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, n));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, kept_bytes));
    return a;
}

void ttm_init_bitmap(mrb_state* mrb)
{
    px_mrb = mrb;
    struct RClass* c = mrb_define_class(mrb, "Bitmap", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_class_method(mrb, c, "ttm_kept", bitmap_s_kept, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, c, "ttm_decoded", bitmap_s_decoded, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "ttm_checksum", bitmap_checksum, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "ttm_resident?", bitmap_resident, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "initialize", bitmap_init, MRB_ARGS_ARG(1, 1));
    mrb_define_method(mrb, c, "initialize_copy", bitmap_copy, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "dispose", bitmap_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", bitmap_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "width", bitmap_width, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "height", bitmap_height, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "rect", bitmap_rect, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "blt", bitmap_blt, MRB_ARGS_ARG(4, 1));
    mrb_define_method(mrb, c, "stretch_blt", bitmap_stretch_blt, MRB_ARGS_ARG(3, 1));
    mrb_define_method(mrb, c, "fill_rect", bitmap_fill_rect, MRB_ARGS_ANY());
    mrb_define_method(mrb, c, "clear", bitmap_clear, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "get_pixel", bitmap_get_pixel, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, c, "set_pixel", bitmap_set_pixel, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, c, "hue_change", bitmap_hue_change, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "draw_text", bitmap_draw_text, MRB_ARGS_ANY());
    mrb_define_method(mrb, c, "text_size", bitmap_text_size, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "font", bitmap_font, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "font=", bitmap_set_font, MRB_ARGS_REQ(1));
}
