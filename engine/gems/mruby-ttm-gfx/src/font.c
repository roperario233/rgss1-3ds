/*
 * font.c -- text for Bitmap#draw_text / #text_size, with stb_truetype.
 *
 * Faces, by name. The game asks RGSS for "Arial", "Times New Roman" (the PC
 * version of To the Moon substitutes Open Sans for both), "NanumBarunGothic"
 * (Korean) and "WenQuanYi Zen Hei" (Chinese), and picks between them per row:
 * Scene_Language draws every language's own name in that language's font. So
 * one face is the default -- the game's own font file, handed over at boot as
 * Font.ttm_face = bytes -- and any other name can be given its own TTF with
 * Font.ttm_face(name, bytes); a name with no file of its own draws in the
 * default face, which is what makes the Latin translations work with Open Sans.
 *
 * Each face keeps its own glyph cache; sizes are RGSS font sizes, mapped to
 * the em, as Windows did.
 *
 * Like RGSS, text that is wider than its rectangle is squeezed horizontally to
 * fit, and it is centred vertically in the rectangle.
 */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include "gfx.h"
#include "stb_truetype.h"

typedef struct glyph {
    int cp, size;
    int w, h, xoff, yoff;
    float adv;               /* pixels */
    unsigned char* bm;       /* w*h coverage */
    struct glyph* next;
} glyph;

#define GLYPH_HASH 1024

typedef struct face {
    char* name;              /* NULL for the default face */
    unsigned char* data;     /* the TTF, kept: stb_truetype reads from it */
    stbtt_fontinfo info;
    int ok;
    int ascent, descent, line_gap;      /* font units */
    glyph* glyphs[GLYPH_HASH];
    struct face* next;
} face;

static face* faces;          /* every face that has a file of its own */
static face* def_face;       /* what a name with no face of its own draws in */

static void face_free(face* f)
{
    if (!f) return;
    for (int i = 0; i < GLYPH_HASH; ++i) {
        glyph* g = f->glyphs[i];
        while (g) {
            glyph* n = g->next;
            stbtt_FreeBitmap(g->bm, NULL);
            free(g);
            g = n;
        }
    }
    free(f->data);
    free(f->name);
    free(f);
}

static int name_is(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    return strcasecmp(a, b) == 0;
}

/* the face a Font#name draws in: its own, or the default */
static face* face_for(const char* name)
{
    if (name && *name)
        for (face* f = faces; f; f = f->next)
            if (name_is(f->name, name)) return f;
    return def_face;
}

static float scale_for(const face* f, int size)
{
    return stbtt_ScaleForMappingEmToPixels((stbtt_fontinfo*)&f->info, (float)size);
}

static glyph* get_glyph(face* f, int cp, int size)
{
    unsigned h = ((unsigned)cp * 31u + (unsigned)size) & (GLYPH_HASH - 1);
    for (glyph* g = f->glyphs[h]; g; g = g->next)
        if (g->cp == cp && g->size == size) return g;
    glyph* g = (glyph*)calloc(1, sizeof(glyph));
    g->cp = cp;
    g->size = size;
    float sc = scale_for(f, size);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    g->adv = adv * sc;
    g->bm = stbtt_GetCodepointBitmap(&f->info, 0, sc, cp, &g->w, &g->h, &g->xoff, &g->yoff);
    g->next = f->glyphs[h];
    f->glyphs[h] = g;
    return g;
}

/* next UTF-8 code point; invalid bytes come out as themselves */
static int utf8_next(const unsigned char** p, const unsigned char* end)
{
    const unsigned char* s = *p;
    int c = *s++;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (n && s + n <= end) {
        c &= (0x3f >> n);
        for (int i = 0; i < n; ++i) c = (c << 6) | (s[i] & 0x3f);
        s += n;
    }
    *p = s;
    return c;
}

int ttm_text_height(const char* font_name, int size)
{
    const face* f = face_for(font_name);
    if (!f || !f->ok) return size;
    return (int)ceilf((f->ascent - f->descent) * scale_for(f, size));
}

static float text_width_f(const face* f, const char* str, int len, int size)
{
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    float sc = scale_for(f, size), x = 0;
    int prev = 0;
    while (p < end) {
        int cp = utf8_next(&p, end);
        if (prev) x += stbtt_GetCodepointKernAdvance((stbtt_fontinfo*)&f->info, prev, cp) * sc;
        x += get_glyph((face*)f, cp, size)->adv;
        prev = cp;
    }
    return x;
}

int ttm_text_width(const char* font_name, const char* str, int len, int size)
{
    const face* f = face_for(font_name);
    if (!f || !f->ok) return len * size / 2;
    return (int)ceilf(text_width_f(f, str, len, size));
}

void ttm_text_draw(ttm_bitmap* b, const char* font_name, int x, int y, int w, int h,
                   const char* str, int len, int size, int bold, const uint8_t color[4], int align)
{
    face* f = face_for(font_name);
    if (!f || !f->ok || len <= 0 || size <= 0 || color[3] == 0) return;
    int tw = (int)ceilf(text_width_f(f, str, len, size)) + (bold ? 1 : 0) + 1;
    int th = ttm_text_height(font_name, size);
    if (tw <= 0 || th <= 0) return;

    /* 1. the line at natural size, as coverage */
    unsigned char* cov = (unsigned char*)calloc((size_t)tw * th, 1);
    float sc = scale_for(f, size), pen = 0;
    int base = (int)roundf(f->ascent * sc);
    const unsigned char* p = (const unsigned char*)str;
    const unsigned char* end = p + len;
    int prev = 0;
    while (p < end) {
        int cp = utf8_next(&p, end);
        if (prev) pen += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * sc;
        glyph* g = get_glyph(f, cp, size);
        int gx = (int)roundf(pen) + g->xoff, gy = base + g->yoff;
        for (int pass = 0; pass <= (bold ? 1 : 0); ++pass) {
            for (int j = 0; j < g->h; ++j) {
                int yy = gy + j;
                if (yy < 0 || yy >= th) continue;
                for (int i = 0; i < g->w; ++i) {
                    int xx = gx + i + pass;
                    if (xx < 0 || xx >= tw) continue;
                    unsigned char c = g->bm[j * g->w + i];
                    unsigned char* d = &cov[yy * tw + xx];
                    if (c > *d) *d = c;
                }
            }
        }
        pen += g->adv;
        prev = cp;
    }

    /* 2. squeeze to the rectangle's width if needed, align, centre vertically */
    float squeeze = 1.0f;
    int ow = tw;
    if (w > 0 && tw > w) {
        squeeze = (float)w / tw;
        ow = w;
    }
    int ox = x;
    if (align == 1) ox = x + (w - ow) / 2;
    else if (align == 2) ox = x + w - ow;
    int oy = y + (h - th) / 2;

    /* 3. blend the colour onto the bitmap, coverage as alpha */
    for (int j = 0; j < th; ++j) {
        int yy = oy + j;
        if (yy < 0 || yy >= b->h) continue;
        for (int i = 0; i < ow; ++i) {
            int xx = ox + i;
            if (xx < 0 || xx >= b->w) continue;
            int sx = squeeze < 1.0f ? (int)(i / squeeze) : i;
            if (sx >= tw) sx = tw - 1;
            int a = cov[j * tw + sx] * color[3] / 255;
            if (!a) continue;
            uint32_t* d = &ttm_row(b, yy)[xx];
            int da = (int)(*d >> 24);
            int oa = a + da * (255 - a) / 255;
            int dw = da * (255 - a) / 255;
            int r = (color[0] * a + (int)(*d & 0xff) * dw) / oa;
            int g = (color[1] * a + (int)((*d >> 8) & 0xff) * dw) / oa;
            int bb = (color[2] * a + (int)((*d >> 16) & 0xff) * dw) / oa;
            *d = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bb << 16) | ((uint32_t)oa << 24);
        }
    }
    free(cov);
}

/* Font.ttm_face = bytes           the default face (a name with no file of its own)
 * Font.ttm_face(name, bytes)      a face of its own, found by Font#name
 * Font.ttm_face?(name)            whether that name has a face of its own
 * Font.ttm_faces                  the names that do (the boot log, pc/lang_check.rb)
 *
 * A face replaces the one it shares a name with: its old glyphs go with it. */
static mrb_value font_set_face(mrb_state* mrb, mrb_value self)
{
    mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    /* one argument: the default face; two: a face for Font#name */
    if (argc != 1 && argc != 2)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Font.ttm_face: bytes, or a name and bytes");
    mrb_value name = argc == 2 ? argv[0] : mrb_nil_value();
    mrb_value data = argv[argc - 1];
    if (!mrb_string_p(data)) mrb_raise(mrb, E_ARGUMENT_ERROR, "Font.ttm_face: no font data");

    const char* want = mrb_nil_p(name) || RSTRING_LEN(name) == 0 ? NULL : RSTRING_PTR(name);
    face* f = NULL;
    if (!want) {
        f = def_face;
    } else {
        for (face* i = faces; i; i = i->next)
            if (name_is(i->name, want)) { f = i; break; }
    }
    if (!f) {
        f = (face*)calloc(1, sizeof(face));
        if (!f) mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory for a font face");
        if (want) {                     /* copied: the caller's String is collected */
            size_t n = strlen(want);
            f->name = (char*)malloc(n + 1);
            memcpy(f->name, want, n + 1);
        }
        f->next = faces;
        if (want) faces = f;
    }

    /* the bytes are copied: stb_truetype reads from them for the whole run,
     * and the caller's String is collected long before that */
    size_t len = (size_t)RSTRING_LEN(data);
    unsigned char* copy = (unsigned char*)malloc(len);
    if (!copy) mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory for a font face");
    memcpy(copy, RSTRING_PTR(data), len);

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, copy, stbtt_GetFontOffsetForIndex(copy, 0))) {
        free(copy);
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "Font.ttm_face: not a TrueType font: %s",
                   want ? want : "(default)");
    }

    /* the face's old glyphs were made from bytes that are about to go */
    for (int i = 0; i < GLYPH_HASH; ++i) {
        glyph* g = f->glyphs[i];
        while (g) {
            glyph* n = g->next;
            stbtt_FreeBitmap(g->bm, NULL);
            free(g);
            g = n;
        }
        f->glyphs[i] = NULL;
    }
    free(f->data);
    f->info = info;
    f->data = copy;
    f->ok = 1;
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);
    if (!def_face) def_face = f;
    return data;
}

static mrb_value font_face_p(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    const char* want = RSTRING_LEN(name) == 0 ? NULL : RSTRING_PTR(name);
    if (!want) return mrb_bool_value(0);
    for (face* f = faces; f; f = f->next)
        if (name_is(f->name, want)) return mrb_bool_value(1);
    return mrb_bool_value(0);
}

static mrb_value font_faces(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new(mrb);
    for (face* f = faces; f; f = f->next)
        if (f->name) mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, f->name));
    return a;
}

void ttm_font_init(mrb_state* mrb)
{
    /* Font itself is a Ruby class (name, size, bold, italic, color); C adds the faces */
    struct RClass* font = mrb_class_defined(mrb, "Font")
                              ? mrb_class_get(mrb, "Font")
                              : mrb_define_class(mrb, "Font", mrb->object_class);
    mrb_define_class_method(mrb, font, "ttm_face=", font_set_face, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, font, "ttm_face", font_set_face, MRB_ARGS_ARG(1, 1));
    mrb_define_class_method(mrb, font, "ttm_face?", font_face_p, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, font, "ttm_faces", font_faces, MRB_ARGS_NONE());
}

