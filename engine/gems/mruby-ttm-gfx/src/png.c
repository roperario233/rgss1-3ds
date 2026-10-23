/*
 * png.c -- PNG decoded row by row straight into a bitmap's bands.
 *
 * stb_image inflates a whole PNG into one buffer (w*h*4 + h bytes), then makes
 * the pixels in a second one of w*h*4: for the 256x6096 Tileset two blocks of
 * 6 MB each, at once, which a fragmented 3DS heap could not find with 40 MB
 * free (console run 8: the tileset, town2, the light sheet and even a
 * 732x552 fog all failed with "outofmem"). Here the IDAT stream is inflated
 * one scanline at a time (zlib), unfiltered against the previous scanline and
 * written into its band: the decode needs two scanlines and zlib's 40 KB on
 * top of the pixels, and the pixels themselves are bands of 128 KB at most.
 *
 * Every PNG in To the Moon is 8-bit grey, RGB or RGBA, or palette at 1, 2, 4
 * or 8 bits, none interlaced; those are decoded here, to exactly the pixels
 * stb gives (pc/png_check.rb compares the two on every image in the archive).
 * Anything else -- 16-bit, interlaced -- returns 0 and goes to stb.
 */

#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "gfx.h"

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static const unsigned char SIGNATURE[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

int ttm_png_info(const unsigned char* d, size_t len, int* w, int* h)
{
    if (len < 33 || memcmp(d, SIGNATURE, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0) return 0;
    uint32_t pw = be32(d + 16), ph = be32(d + 20);
    if (pw == 0 || ph == 0 || pw > 0x7fffffff || ph > 0x7fffffff) return 0;
    *w = (int)pw;
    *h = (int)ph;
    return 1;
}

#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

typedef struct {
    int w, h, depth, ctype;
    size_t rowbytes;     /* one scanline's bytes, without its filter byte */
    int fbpp;            /* bytes per pixel for the filters (at least 1) */
    uint32_t pal[256];   /* RGBA, alpha from tRNS */
    int trns;            /* grey or RGB: a colour key is set */
    unsigned trns_rgb[3];
} png_info;

static unsigned char paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return (unsigned char)a;
    return (unsigned char)(pb <= pc ? b : c);
}

/* undo the scanline's filter in place (row[0] is the filter type); 0 if the
 * type is not one of PNG's five */
static int unfilter(const png_info* pi, unsigned char* row, const unsigned char* prev)
{
    unsigned char* x = row + 1;
    const unsigned char* p = prev + 1;
    size_t n = pi->rowbytes, bpp = (size_t)pi->fbpp;
    switch (row[0]) {
    case 0: break;
    case 1: for (size_t i = bpp; i < n; ++i) x[i] = (unsigned char)(x[i] + x[i - bpp]); break;
    case 2: for (size_t i = 0; i < n; ++i) x[i] = (unsigned char)(x[i] + p[i]); break;
    case 3:
        for (size_t i = 0; i < n; ++i)
            x[i] = (unsigned char)(x[i] + (((i >= bpp ? x[i - bpp] : 0) + p[i]) >> 1));
        break;
    case 4:
        for (size_t i = 0; i < n; ++i)
            x[i] = (unsigned char)(x[i] + paeth(i >= bpp ? x[i - bpp] : 0, p[i], i >= bpp ? p[i - bpp] : 0));
        break;
    default: return 0;
    }
    return 1;
}

/* sample x of a scanline packed at `depth` bits (1, 2, 4), high bits first */
static unsigned packed(const unsigned char* s, int x, int depth)
{
    int bit = x * depth;
    return (s[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
}

/* one unfiltered scanline to RGBA, as stb converts it */
static void to_rgba(const png_info* pi, const unsigned char* s, uint32_t* d)
{
    int w = pi->w;
    switch (pi->ctype) {
    case 6:
        for (int x = 0; x < w; ++x, s += 4) d[x] = RGBA(s[0], s[1], s[2], s[3]);
        break;
    case 2:
        for (int x = 0; x < w; ++x, s += 3) {
            int a = pi->trns && s[0] == pi->trns_rgb[0] && s[1] == pi->trns_rgb[1] && s[2] == pi->trns_rgb[2] ? 0 : 255;
            d[x] = RGBA(s[0], s[1], s[2], a);
        }
        break;
    case 4:
        for (int x = 0; x < w; ++x, s += 2) d[x] = RGBA(s[0], s[0], s[0], s[1]);
        break;
    case 0: {
        /* stb scales 1/2/4-bit grey up to 0..255, and the key with it */
        static const unsigned scale[9] = { 0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01 };
        unsigned k = scale[pi->depth];
        for (int x = 0; x < w; ++x) {
            unsigned v = (pi->depth == 8 ? s[x] : packed(s, x, pi->depth)) * k;
            int a = pi->trns && v == pi->trns_rgb[0] ? 0 : 255;
            d[x] = RGBA(v, v, v, a);
        }
        break;
    }
    case 3:
        for (int x = 0; x < w; ++x) d[x] = pi->pal[pi->depth == 8 ? s[x] : packed(s, x, pi->depth)];
        break;
    }
}

static void drop_pixels(ttm_bitmap* b) { ttm_bitmap_free_px(b); }

int ttm_png_decode(const unsigned char* d, size_t len, ttm_bitmap* b, const char** why)
{
    png_info pi;
    if (!ttm_png_info(d, len, &pi.w, &pi.h)) return 0;
    pi.depth = d[24];
    pi.ctype = d[25];
    int interlace = d[28];
    int channels;
    switch (pi.ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return 0;
    }
    int ok_depth = pi.depth == 8 || ((pi.ctype == 0 || pi.ctype == 3) && (pi.depth == 1 || pi.depth == 2 || pi.depth == 4));
    if (!ok_depth || interlace != 0 || d[26] != 0 || d[27] != 0) return 0;    /* 16-bit, Adam7: stb */
    if (pi.w > 16384 || pi.h > 16384) {
        *why = "too large";
        return -1;
    }
    pi.rowbytes = ((size_t)pi.w * channels * pi.depth + 7) / 8;
    pi.fbpp = channels * pi.depth / 8 > 0 ? channels * pi.depth / 8 : 1;
    for (int i = 0; i < 256; ++i) pi.pal[i] = RGBA(0, 0, 0, 255);
    pi.trns = 0;

    unsigned char* rows = (unsigned char*)calloc(2, pi.rowbytes + 1);
    if (!rows || !ttm_bitmap_alloc(b, pi.w, pi.h)) {
        free(rows);
        *why = "outofmem";
        return -1;
    }
    unsigned char *cur = rows, *prev = rows + pi.rowbytes + 1;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        free(rows);
        drop_pixels(b);
        *why = "outofmem";
        return -1;
    }
    int y = 0, done = 0;
    size_t fill = 0;
    const char* fail = NULL;
    size_t at = 8;
    while (!fail && !done && at + 12 <= len) {
        uint32_t clen = be32(d + at);
        const unsigned char* type = d + at + 4;
        const unsigned char* body = d + at + 8;
        if (clen > len - at - 12) {
            fail = "corrupt PNG";
            break;
        }
        at += 12 + (size_t)clen;
        if (!memcmp(type, "PLTE", 4)) {
            for (uint32_t i = 0; i < clen / 3 && i < 256; ++i)
                pi.pal[i] = RGBA(body[i * 3], body[i * 3 + 1], body[i * 3 + 2], 255);
        } else if (!memcmp(type, "tRNS", 4)) {
            if (pi.ctype == 3) {
                for (uint32_t i = 0; i < clen && i < 256; ++i)
                    pi.pal[i] = (pi.pal[i] & 0x00ffffffu) | ((uint32_t)body[i] << 24);
            } else if (pi.ctype == 0 && clen >= 2) {
                static const unsigned scale[9] = { 0, 0xff, 0x55, 0, 0x11, 0, 0, 0, 0x01 };
                pi.trns = 1;
                pi.trns_rgb[0] = (body[1] & 0xff) * scale[pi.depth];
            } else if (pi.ctype == 2 && clen >= 6) {
                pi.trns = 1;
                for (int k = 0; k < 3; ++k) pi.trns_rgb[k] = body[k * 2 + 1];
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            zs.next_in = (Bytef*)body;
            zs.avail_in = clen;
            while (zs.avail_in > 0 && !done) {
                zs.next_out = cur + fill;
                zs.avail_out = (uInt)(pi.rowbytes + 1 - fill);
                int rc = inflate(&zs, Z_NO_FLUSH);
                fill = pi.rowbytes + 1 - zs.avail_out;
                if (fill == pi.rowbytes + 1) {
                    if (y < pi.h) {
                        if (!unfilter(&pi, cur, prev)) {
                            fail = "corrupt PNG";
                            break;
                        }
                        to_rgba(&pi, cur + 1, ttm_row(b, y));
                        y++;
                    }
                    unsigned char* t = prev;
                    prev = cur;
                    cur = t;
                    fill = 0;
                }
                if (rc == Z_STREAM_END) {
                    done = 1;
                } else if (rc == Z_MEM_ERROR) {
                    fail = "outofmem";
                    break;
                } else if (rc != Z_OK && rc != Z_BUF_ERROR) {
                    fail = "corrupt PNG";
                    break;
                } else if (rc == Z_BUF_ERROR && zs.avail_in > 0 && zs.avail_out > 0) {
                    fail = "corrupt PNG";    /* no progress with input and room left */
                    break;
                }
            }
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
    }
    inflateEnd(&zs);
    free(rows);
    if (!fail && y < pi.h) fail = "corrupt PNG";    /* stb: "not enough pixels" */
    if (fail) {
        drop_pixels(b);
        *why = fail;
        return -1;
    }
    return 1;
}
