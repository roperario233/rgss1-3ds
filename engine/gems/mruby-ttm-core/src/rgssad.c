/*
 * rgssad.c -- read files out of an RPG Maker XP archive (.rgssad v1).
 *
 * The player's own "To the Moon.rgssad" stays as it is on the SD card; this
 * indexes it once and decrypts single entries on demand. Format, as verified
 * byte-for-byte against the game's archive by tools/rgssad_extract.py:
 *
 *     "RGSSAD\0" 0x01
 *     key = 0xDEADCAFE
 *     repeat:
 *         name_len = le_u32 ^ key            key = key*7 + 3
 *         name[i]  = byte ^ (key & 0xff)     key = key*7 + 3   (per byte)
 *         size     = le_u32 ^ key            key = key*7 + 3
 *         payload: each 4-byte chunk ^= data_key (LE), data_key = data_key*7 + 3,
 *                  starting from data_key = key (the table key does not advance)
 *
 * Lookups are case-insensitive and treat '\' and '/' alike, as RGSS does.
 *
 * TTM.archive_open(path[, cache]) -> number of entries (raises on a bad archive)
 *
 * Indexing walks every entry's header: 979 seeks and reads across the 36 MB
 * archive, 1.9 s on the 3DS's SD card. With a cache path the finished index is
 * saved there and used on the next start (a few KB, one read), as long as the
 * archive's size and the CRC of its first and last 4 KB still match. (The
 * card's files have no usable mtime on the 3DS.)
 * TTM.archive_read(name)      -> String, or nil if absent
 * TTM.archive_has?(name)      -> true/false
 * TTM.archive_png_size(name)  -> [width, height] from the PNG header, or nil
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
    uint32_t offset;
    uint32_t size;
    uint32_t data_key;
} entry_t;

static FILE* arc;
static entry_t* entries;
static size_t n_entries;

static void normalise(char* s)
{
    for (; *s; ++s) *s = (*s == '\\') ? '/' : (char)tolower((unsigned char)*s);
}

static int entry_cmp(const void* a, const void* b)
{
    return strcmp(((const entry_t*)a)->key_name, ((const entry_t*)b)->key_name);
}

static void close_archive(void)
{
    for (size_t i = 0; i < n_entries; ++i) free(entries[i].key_name);
    free(entries);
    entries = NULL;
    n_entries = 0;
    if (arc) fclose(arc);
    arc = NULL;
}


/* ---- the index cache ------------------------------------------------------------- */

#define CACHE_MAGIC "TTMX"
#define CACHE_VERSION 1u

static int cache_hit;    /* the last archive_open used its cache */

static uint32_t fingerprint(long size)
{
    uint8_t b[4096];
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t n;
    fseek(arc, 0, SEEK_SET);
    n = fread(b, 1, sizeof(b), arc);
    crc = crc32(crc, b, (uInt)n);
    if (size > (long)sizeof(b)) {
        fseek(arc, size - (long)sizeof(b), SEEK_SET);
        n = fread(b, 1, sizeof(b), arc);
        crc = crc32(crc, b, (uInt)n);
    }
    return (uint32_t)crc;
}

static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* "TTMX" version size fingerprint count, then per entry: u16 name length,
 * name, offset, size, data key (already sorted) */
static int cache_load(const char* path, uint32_t size, uint32_t fp)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = n > 20 ? (uint8_t*)malloc((size_t)n) : NULL;
    int ok = buf && fread(buf, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok || memcmp(buf, CACHE_MAGIC, 4) || get32(buf + 4) != CACHE_VERSION || get32(buf + 8) != size ||
        get32(buf + 12) != fp) {
        free(buf);
        return 0;
    }
    uint32_t count = get32(buf + 16);
    entry_t* e = (entry_t*)calloc(count ? count : 1, sizeof(entry_t));
    const uint8_t* p = buf + 20;
    const uint8_t* end = buf + n;
    uint32_t i = 0;
    for (; i < count && p + 2 <= end; ++i) {
        int len = p[0] | p[1] << 8;
        if (p + 2 + len + 12 > end) break;
        e[i].key_name = (char*)malloc((size_t)len + 1);
        memcpy(e[i].key_name, p + 2, (size_t)len);
        e[i].key_name[len] = 0;
        p += 2 + len;
        e[i].offset = get32(p);
        e[i].size = get32(p + 4);
        e[i].data_key = get32(p + 8);
        p += 12;
    }
    free(buf);
    if (i != count) {    /* short: not a cache to trust */
        for (uint32_t j = 0; j < i; ++j) free(e[j].key_name);
        free(e);
        return 0;
    }
    entries = e;
    n_entries = count;
    return 1;
}

static void cache_save(const char* path, uint32_t size, uint32_t fp)
{
    size_t bytes = 20;
    for (size_t i = 0; i < n_entries; ++i) bytes += 2 + strlen(entries[i].key_name) + 12;
    uint8_t* buf = (uint8_t*)malloc(bytes);
    if (!buf) return;
    memcpy(buf, CACHE_MAGIC, 4);
    put32(buf + 4, CACHE_VERSION);
    put32(buf + 8, size);
    put32(buf + 12, fp);
    put32(buf + 16, (uint32_t)n_entries);
    uint8_t* p = buf + 20;
    for (size_t i = 0; i < n_entries; ++i) {
        size_t len = strlen(entries[i].key_name);
        p[0] = (uint8_t)len;
        p[1] = (uint8_t)(len >> 8);
        memcpy(p + 2, entries[i].key_name, len);
        p += 2 + len;
        put32(p, entries[i].offset);
        put32(p + 4, entries[i].size);
        put32(p + 8, entries[i].data_key);
        p += 12;
    }
    FILE* f = fopen(path, "wb");
    if (f) {
        int ok = fwrite(buf, 1, bytes, f) == bytes;
        if (fclose(f) != 0) ok = 0;
        if (!ok) remove(path);    /* a half-written cache is never read as one */
    }
    free(buf);
}

static mrb_value archive_open(mrb_state* mrb, mrb_value self)
{
    const char* path;
    const char* cache = NULL;
    mrb_get_args(mrb, "z|z!", &path, &cache);
    close_archive();
    cache_hit = 0;

    arc = fopen(path, "rb");
    if (!arc) mrb_raisef(mrb, E_RUNTIME_ERROR, "cannot open archive %s", path);
    char magic[8];
    if (fread(magic, 1, 8, arc) != 8 || memcmp(magic, "RGSSAD\0", 7) != 0 || magic[7] != 1) {
        close_archive();
        mrb_raisef(mrb, E_RUNTIME_ERROR, "not an RGSSAD v1 archive: %s", path);
    }
    fseek(arc, 0, SEEK_END);
    long arc_size = ftell(arc);
    uint32_t fp = cache ? fingerprint(arc_size) : 0;
    if (cache && cache_load(cache, (uint32_t)arc_size, fp)) {
        cache_hit = 1;
        setvbuf(arc, NULL, _IOFBF, 4096);
        return mrb_int_value(mrb, (mrb_int)n_entries);
    }

    /* One seek and one read per entry: its header (length, name, size) is at
     * most 4 + 1024 + 4 bytes. On the 3DS every SD read is a service call, so
     * this is what keeps indexing ~1000 entries short. */
    setvbuf(arc, NULL, _IOFBF, 4096);
    size_t cap = 1024;
    entries = (entry_t*)malloc(cap * sizeof(entry_t));
    uint32_t key = 0xDEADCAFEu;
    uint8_t hdr[4 + 1024 + 4];
    long pos = 8;
    for (;;) {
        if (fseek(arc, pos, SEEK_SET) != 0) break;
        size_t got = fread(hdr, 1, sizeof(hdr), arc);
        if (got < 4) break;                              /* end of file */
        uint32_t name_len = ((uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                             ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24)) ^ key;
        key = key * 7 + 3;
        if (name_len == 0) break;                        /* terminator */
        if (name_len > 1024 || got < 4 + name_len + 4) {
            close_archive();
            mrb_raise(mrb, E_RUNTIME_ERROR, "corrupt archive (entry header)");
        }
        char* name = (char*)malloc(name_len + 1);
        for (uint32_t i = 0; i < name_len; ++i) {
            name[i] = (char)(hdr[4 + i] ^ (uint8_t)(key & 0xff));
            key = key * 7 + 3;
        }
        name[name_len] = '\0';
        normalise(name);

        const uint8_t* s = hdr + 4 + name_len;
        uint32_t size = ((uint32_t)s[0] | ((uint32_t)s[1] << 8) |
                         ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24)) ^ key;
        key = key * 7 + 3;

        if (n_entries == cap) {
            cap *= 2;
            entries = (entry_t*)realloc(entries, cap * sizeof(entry_t));
        }
        entries[n_entries].key_name = name;
        entries[n_entries].offset = (uint32_t)(pos + 4 + name_len + 4);
        entries[n_entries].size = size;
        entries[n_entries].data_key = key;
        ++n_entries;
        pos = (long)entries[n_entries - 1].offset + (long)size;
    }
    qsort(entries, n_entries, sizeof(entry_t), entry_cmp);
    if (cache) cache_save(cache, (uint32_t)arc_size, fp);
    return mrb_int_value(mrb, (mrb_int)n_entries);
}

/* TTM.archive_cached? -> whether the last archive_open read its index from the cache */
static mrb_value archive_cached(mrb_state* mrb, mrb_value self) { return mrb_bool_value(cache_hit); }

/* TTM.archive_entries -> [[name, offset, size, data key], ...] in index order (pc/boot_check.rb) */
static mrb_value archive_entries(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new_capa(mrb, (mrb_int)n_entries);
    for (size_t i = 0; i < n_entries; ++i) {
        mrb_value e[4] = { mrb_str_new_cstr(mrb, entries[i].key_name), mrb_int_value(mrb, entries[i].offset),
                           mrb_int_value(mrb, entries[i].size), mrb_int_value(mrb, entries[i].data_key) };
        mrb_ary_push(mrb, a, mrb_ary_new_from_values(mrb, 4, e));
    }
    return a;
}

static const entry_t* find(mrb_state* mrb, mrb_value name)
{
    if (!arc) return NULL;
    char buf[512];
    mrb_int len = RSTRING_LEN(name);
    if (len >= (mrb_int)sizeof(buf)) return NULL;
    memcpy(buf, RSTRING_PTR(name), (size_t)len);
    buf[len] = '\0';
    normalise(buf);
    entry_t probe;
    probe.key_name = buf;
    return (const entry_t*)bsearch(&probe, entries, n_entries, sizeof(entry_t), entry_cmp);
}

/* decrypt n bytes of an entry, starting at its first byte */
static void decrypt(uint8_t* p, size_t n, uint32_t data_key)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        p[i] ^= (uint8_t)data_key;
        p[i + 1] ^= (uint8_t)(data_key >> 8);
        p[i + 2] ^= (uint8_t)(data_key >> 16);
        p[i + 3] ^= (uint8_t)(data_key >> 24);
        data_key = data_key * 7 + 3;
    }
    for (int j = 0; i < n; ++i, ++j) p[i] ^= (uint8_t)(data_key >> (8 * j));
}

static mrb_value archive_read(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    const entry_t* e = find(mrb, name);
    if (!e) return mrb_nil_value();
    mrb_value str = mrb_str_new(mrb, NULL, e->size);
    if (fseek(arc, (long)e->offset, SEEK_SET) != 0)
        mrb_raise(mrb, E_RUNTIME_ERROR, "archive seek failed");
    /* bounded chunks: one ~1 MB fread on the 3DS SD card returned bad data */
    char* dst = RSTRING_PTR(str);
    size_t done = 0;
    while (done < e->size) {
        size_t want = e->size - done;
        if (want > 65536) want = 65536;
        size_t got = fread(dst + done, 1, want, arc);
        if (got == 0) mrb_raisef(mrb, E_RUNTIME_ERROR, "archive read failed at %d of %d",
                                 (int)done, (int)e->size);
        done += got;
    }
    decrypt((uint8_t*)RSTRING_PTR(str), e->size, e->data_key);
    return str;
}

static mrb_value archive_has(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    return mrb_bool_value(find(mrb, name) != NULL);
}

static mrb_value archive_png_size(mrb_state* mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "S", &name);
    const entry_t* e = find(mrb, name);
    if (!e || e->size < 24) return mrb_nil_value();
    uint8_t h[24];
    if (fseek(arc, (long)e->offset, SEEK_SET) != 0 || fread(h, 1, 24, arc) != 24)
        return mrb_nil_value();
    decrypt(h, 24, e->data_key);
    if (memcmp(h, "\x89PNG", 4) != 0) return mrb_nil_value();
    mrb_int w = ((mrb_int)h[16] << 24) | ((mrb_int)h[17] << 16) | ((mrb_int)h[18] << 8) | h[19];
    mrb_int ht = ((mrb_int)h[20] << 24) | ((mrb_int)h[21] << 16) | ((mrb_int)h[22] << 8) | h[23];
    mrb_value pair[2] = { mrb_int_value(mrb, w), mrb_int_value(mrb, ht) };
    return mrb_ary_new_from_values(mrb, 2, pair);
}

void ttm_init_archive(mrb_state* mrb, struct RClass* ttm)
{
    mrb_define_module_function(mrb, ttm, "archive_open", archive_open, MRB_ARGS_ARG(1, 1));
    mrb_define_module_function(mrb, ttm, "archive_cached?", archive_cached, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "archive_entries", archive_entries, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "archive_read", archive_read, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "archive_has?", archive_has, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "archive_png_size", archive_png_size, MRB_ARGS_REQ(1));
}

void ttm_final_archive(void)
{
    close_archive();
}
