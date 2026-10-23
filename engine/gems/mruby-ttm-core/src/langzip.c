/*
 * langzip.c -- the game's localisation pack, lang.dat, read as a ZIP.
 *
 * lang.dat is not game data: it is the install folder's ZIP of translations,
 * one folder per language, and mkxp mounts it beside the .rgssad so that the
 * game's own TR module (118_TTMTranslator) finds "lang/index.rxdata" and
 * "lang/<code>/..." as ordinary game files. This does the same:
 *
 *     lang/index.rxdata            the languages and the localised pictures
 *     lang/<code>/lang.rxdata      the dialogue, one block per event
 *     lang/<code>/meta.rxdata      the menu's words, and the language's font
 *     lang/<code>/Graphics/...     the pictures with text in them
 *
 * The reader parses the central directory once (851 entries, one read) and
 * inflates single entries on demand; the file itself is never unpacked, so the
 * pack costs one open and no RAM until something is drawn from it. Entries are
 * stored or deflated, and lookups are case-insensitive with '\' and '/'
 * alike, as on the .rgssad side.
 *
 * TTM.lang_open(path)   -> number of entries (raises if it is not a ZIP)
 * TTM.lang_read(name)   -> String, or nil if absent
 * TTM.lang_has?(name)   -> true/false
 * TTM.lang_files        -> [name, ...] (no folders), in index order
 * TTM.lang_close
 *
 * main.c closes nothing on the way out (the process ends); TTM.lang_close is
 * here for the PC checks, which open more than one pack per run.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/string.h>

typedef struct {
    char* key_name;      /* normalised: lower case, '/' separators */
    uint32_t offset;     /* where the entry's data starts */
    uint32_t csize;      /* bytes in the file */
    uint32_t usize;      /* bytes once inflated */
    uint16_t method;     /* 0 stored, 8 deflated */
} lz_entry;

static FILE* pack;
static lz_entry* entries;
static size_t n_entries;

#define EOCD_SIG "\x50\x4b\x05\x06"
#define CDIR_SIG "\x50\x4b\x01\x02"
#define LOCAL_SIG "\x50\x4b\x03\x04"
#define EOCD_MIN 22
#define EOCD_MAX (EOCD_MIN + 65535)     /* + the comment a ZIP may carry */

static void normalise(char* s)
{
    for (; *s; ++s) *s = (*s == '\\') ? '/' : (char)tolower((unsigned char)*s);
}

static int entry_cmp(const void* a, const void* b)
{
    return strcmp(((const lz_entry*)a)->key_name, ((const lz_entry*)b)->key_name);
}

static void close_pack(void)
{
    for (size_t i = 0; i < n_entries; ++i) free(entries[i].key_name);
    free(entries);
    entries = NULL;
    n_entries = 0;
    if (pack) fclose(pack);
    pack = NULL;
}

static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* a bounded read at a time: one ~1 MB fread on the 3DS SD card returned bad
 * data (rgssad.c), and an entry here can be 4 MB */
static int read_at(long pos, void* dst, size_t n)
{
    if (fseek(pack, pos, SEEK_SET) != 0) return 0;
    uint8_t* p = (uint8_t*)dst;
    while (n) {
        size_t want = n > 65536 ? 65536 : n;
        size_t got = fread(p, 1, want, pack);
        if (got == 0) return 0;
        p += got;
        n -= got;
    }
    return 1;
}

static mrb_value lang_open(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    close_pack();

    pack = fopen(path, "rb");
    if (!pack) mrb_raisef(mrb, E_RUNTIME_ERROR, "cannot open language pack %s", path);
    if (fseek(pack, 0, SEEK_END) != 0) {
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "cannot read language pack %s", path);
    }
    long size = ftell(pack);
    if (size < EOCD_MIN) {
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "not a ZIP (too short): %s", path);
    }

    /* the end-of-central-directory record, scanned back from the end (a ZIP
     * may carry a comment after it) */
    long tail = size > EOCD_MAX ? (long)EOCD_MAX : size;
    uint8_t* buf = (uint8_t*)malloc((size_t)tail);
    if (!buf || !read_at(size - tail, buf, (size_t)tail)) {
        free(buf);
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "cannot read language pack %s", path);
    }
    long eocd = -1;
    for (long i = tail - EOCD_MIN; i >= 0; --i)
        if (memcmp(buf + i, EOCD_SIG, 4) == 0) { eocd = i; break; }
    if (eocd < 0) {
        free(buf);
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "not a ZIP (no central directory): %s", path);
    }
    uint32_t count = le16(buf + eocd + 10);
    uint32_t cd_size = le32(buf + eocd + 12);
    uint32_t cd_off = le32(buf + eocd + 16);
    free(buf);
    if (cd_off == 0xffffffffu || count == 0xffffu) {     /* ZIP64: not what lang.dat is */
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "ZIP64 language pack is not supported: %s", path);
    }
    if ((long)cd_off + (long)cd_size > size) {
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "corrupt language pack (central directory): %s", path);
    }

    buf = (uint8_t*)malloc(cd_size ? cd_size : 1);
    if (!buf || !read_at((long)cd_off, buf, cd_size)) {
        free(buf);
        close_pack();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "cannot read language pack %s", path);
    }
    entries = (lz_entry*)calloc(count ? count : 1, sizeof(lz_entry));
    if (!entries) {
        free(buf);
        close_pack();
        mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory reading the language pack");
    }

    const uint8_t* p = buf;
    const uint8_t* end = buf + cd_size;
    size_t n = 0;
    while (n < count && p + 46 <= end && memcmp(p, CDIR_SIG, 4) == 0) {
        uint16_t method = le16(p + 10);
        uint32_t csize = le32(p + 20);
        uint32_t usize = le32(p + 24);
        uint16_t name_len = le16(p + 28);
        uint16_t extra_len = le16(p + 30);
        uint16_t comment_len = le16(p + 32);
        uint32_t local = le32(p + 42);
        if (p + 46 + name_len > end) break;

        char* name = (char*)malloc((size_t)name_len + 1);
        memcpy(name, p + 46, name_len);
        name[name_len] = '\0';
        normalise(name);
        p += 46 + name_len + extra_len + comment_len;

        size_t len = strlen(name);
        if (len == 0 || name[len - 1] == '/') {          /* a folder, not a file */
            free(name);
            continue;
        }

        /* the data offset needs the *local* header's name/extra lengths: a
         * writer may put a different extra field there */
        uint8_t lh[30];
        if (!read_at((long)local, lh, sizeof(lh)) || memcmp(lh, LOCAL_SIG, 4) != 0) {
            free(name);
            continue;
        }
        entries[n].key_name = name;
        entries[n].offset = local + 30u + le16(lh + 26) + le16(lh + 28);
        entries[n].csize = csize;
        entries[n].usize = usize;
        entries[n].method = method;
        ++n;
    }
    free(buf);
    n_entries = n;      /* a short index is still a working one: bsearch on what parsed */
    qsort(entries, n_entries, sizeof(lz_entry), entry_cmp);
    return mrb_int_value(mrb, (mrb_int)n_entries);
}

static const lz_entry* find(mrb_state* mrb, mrb_value name)
{
    if (!pack) return NULL;
    char buf[512];
    mrb_int len = RSTRING_LEN(name);
    if (len <= 0 || len >= (mrb_int)sizeof(buf)) return NULL;
    memcpy(buf, RSTRING_PTR(name), (size_t)len);
    buf[len] = '\0';
    normalise(buf);
    lz_entry probe;
    probe.key_name = buf;
    return (const lz_entry*)bsearch(&probe, entries, n_entries, sizeof(lz_entry), entry_cmp);
}

static mrb_value lang_read(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    const lz_entry* e = find(mrb, name);
    if (!e) return mrb_nil_value();

    if (e->method == 0) {                    /* stored */
        mrb_value str = mrb_str_new(mrb, NULL, e->usize);
        if (e->usize && !read_at((long)e->offset, RSTRING_PTR(str), e->usize))
            mrb_raisef(mrb, E_RUNTIME_ERROR, "language pack read failed: %s", RSTRING_PTR(name));
        return str;
    }
    if (e->method != 8)
        mrb_raisef(mrb, E_RUNTIME_ERROR, "unsupported ZIP method %d: %s", (int)e->method,
                   RSTRING_PTR(name));

    uint8_t* raw = (uint8_t*)malloc(e->csize ? e->csize : 1);
    if (!raw) mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory reading the language pack");
    if (e->csize && !read_at((long)e->offset, raw, e->csize)) {
        free(raw);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "language pack read failed: %s", RSTRING_PTR(name));
    }
    /* the inflated size is the central directory's, so the String is made
     * first and inflated straight into it (no second copy of 4 MB) */
    mrb_value str = mrb_str_new(mrb, NULL, e->usize);
    z_stream z;
    memset(&z, 0, sizeof(z));
    /* -MAX_WBITS: ZIP stores a raw deflate stream, without zlib's header */
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        free(raw);
        mrb_raise(mrb, E_RUNTIME_ERROR, "zlib could not start");
    }
    z.next_in = raw;
    z.avail_in = e->csize;
    z.next_out = (Bytef*)RSTRING_PTR(str);
    z.avail_out = e->usize;
    int rc = inflate(&z, Z_FINISH);
    uLong made = z.total_out;
    inflateEnd(&z);
    free(raw);
    if (rc != Z_STREAM_END || made != e->usize)
        mrb_raisef(mrb, E_RUNTIME_ERROR, "language pack entry will not inflate: %s",
                   RSTRING_PTR(name));
    return str;
}

static mrb_value lang_has(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    return mrb_bool_value(find(mrb, name) != NULL);
}

/* TTM.lang_files -> the entry names, folders left out (pc/lang_check.rb) */
static mrb_value lang_files(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new_capa(mrb, (mrb_int)n_entries);
    for (size_t i = 0; i < n_entries; ++i)
        mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, entries[i].key_name));
    return a;
}

static mrb_value lang_close(mrb_state* mrb, mrb_value self)
{
    close_pack();
    return mrb_nil_value();
}

void ttm_init_lang(mrb_state* mrb, struct RClass* ttm)
{
    mrb_define_module_function(mrb, ttm, "lang_open", lang_open, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "lang_read", lang_read, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "lang_has?", lang_has, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "lang_files", lang_files, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "lang_close", lang_close, MRB_ARGS_NONE());
}

void ttm_final_lang(void)
{
    close_pack();
}
