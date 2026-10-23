/*
 * ttm_core.c -- engine core for the To the Moon 3DS port, as an mruby gem.
 * Builds for the 3DS and for the PC test build alike.
 *
 * TTM.us            microseconds since start (or TTM.reset_clock), Integer
 * TTM.reset_clock
 * TTM.obj_allocs    objects mruby has allocated so far (counter added to
 *                   gc.c by build_mruby.ps1; wraps at 2**30)
 * TTM.gc_live       objects the GC currently counts as live
 * TTM.read_file(p)  whole file as a String, or nil
 * TTM.inflate(s)    zlib-inflate a String (Scripts.rxdata stores code deflated)
 * TTM.load_script(code, name)  run Ruby source at top level, like RGSS does
 * TTM.load_script_cached(code, name, path)
 *                              the same, through a compiled copy at path: the
 *                              first run compiles and saves the bytecode, the
 *                              next ones load it (the game's 124 scripts take
 *                              2 s to compile on the 3DS)
 * TTM.crc32(str)               zlib's CRC-32 (the cache files' names)
 * TTM.lang_open(path)          the game's lang.dat (a ZIP of translations), read
 *                              entry by entry -- see langzip.c
 *
 * Marshal.load is in marshal.c, the archive reader in rgssad.c, the language
 * pack in langzip.c, and the game's own files (RGSS's File/Dir: write, size,
 * mtime, delete, mkdir, entries) in fileio.c.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/dump.h>
#include <mruby/proc.h>
#include <mruby/string.h>

#if defined(__3DS__)
#include <3ds.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

void ttm_init_marshal(mrb_state* mrb);
void ttm_init_archive(mrb_state* mrb, struct RClass* ttm);
void ttm_init_lang(mrb_state* mrb, struct RClass* ttm);
void ttm_init_fileio(mrb_state* mrb, struct RClass* ttm);
void ttm_final_archive(void);
void ttm_final_lang(void);
void ttm_init_profiler(mrb_state* mrb, struct RClass* ttm);

extern size_t mrb_ttm_obj_allocs;   /* src/gc.c, patched */
/* mruby's own, declared only in its internal header */
int mrb_dump_irep(mrb_state* mrb, const mrb_irep* irep, uint8_t flags, uint8_t** bin, size_t* bin_size);

static uint64_t raw_us(void)
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

static uint64_t clock_base;

static mrb_value ttm_us(mrb_state* mrb, mrb_value self)
{
    /* 30 bits keep it an immediate Integer (no allocation) for ~17 minutes */
    return mrb_int_value(mrb, (mrb_int)((raw_us() - clock_base) & 0x3fffffff));
}

static mrb_value ttm_reset_clock(mrb_state* mrb, mrb_value self)
{
    clock_base = raw_us();
    return mrb_nil_value();
}

static mrb_value ttm_obj_allocs(mrb_state* mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)(mrb_ttm_obj_allocs & 0x3fffffff));
}

static mrb_value ttm_gc_live(mrb_state* mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)mrb->gc.live);
}

static mrb_value ttm_read_file(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    FILE* f = fopen(path, "rb");
    if (!f) return mrb_nil_value();
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    mrb_value str = mrb_str_new(mrb, NULL, n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(RSTRING_PTR(str), 1, (size_t)n, f) : 0;
    fclose(f);
    if (got != (size_t)(n > 0 ? n : 0)) return mrb_nil_value();
    return str;
}

/* Compile and run Ruby source at the top level, as RGSS runs each script in
 * Scripts.rxdata: self is main, defs go to Object, and backtraces name the
 * script. Exceptions propagate to the caller. */
static mrb_value ttm_load_script(mrb_state* mrb, mrb_value self)
{
    mrb_value code;
    const char* name;
    mrb_get_args(mrb, "Sz", &code, &name);
    mrb_ccontext* cxt = mrb_ccontext_new(mrb);
    mrb_ccontext_filename(mrb, cxt, name);
    cxt->capture_errors = TRUE;
    mrb_value result = mrb_load_nstring_cxt(mrb, RSTRING_PTR(code), (size_t)RSTRING_LEN(code), cxt);
    mrb_ccontext_free(mrb, cxt);
    if (mrb->exc) {
        mrb_value exc = mrb_obj_value(mrb->exc);
        mrb->exc = NULL;
        mrb_exc_raise(mrb, exc);
    }
    return result;
}

/* ---- compiled scripts ------------------------------------------------------------------
 * A cached script is mruby bytecode with its debug info (backtraces keep the
 * script's name and line numbers). The file's name carries the CRC of the
 * source it came from, so a changed patch or engine compiles afresh; mruby's
 * own header (format version and CRC) rejects a stale or damaged file, which
 * then compiles from source too. Both paths run the proc the same way
 * mrb_load_nstring_cxt does: at the top level, target class Object. */

static long script_hits, script_misses, script_bad;

static uint8_t* slurp(const char* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = n > 0 ? (uint8_t*)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

static void raise_pending(mrb_state* mrb)
{
    mrb_value exc = mrb_obj_value(mrb->exc);
    mrb->exc = NULL;
    mrb_exc_raise(mrb, exc);
}

static mrb_value ttm_load_script_cached(mrb_state* mrb, mrb_value self)
{
    mrb_value code;
    const char* name;
    const char* path;
    mrb_get_args(mrb, "Szz", &code, &name, &path);
    mrb_value proc = mrb_nil_value();

    size_t n;
    uint8_t* bin = slurp(path, &n);
    if (bin) {
        mrb_ccontext* cxt = mrb_ccontext_new(mrb);
        cxt->no_exec = TRUE;
        proc = mrb_load_irep_buf_cxt(mrb, bin, n, cxt);
        mrb_ccontext_free(mrb, cxt);
        free(bin);
        if (mrb->exc || !mrb_proc_p(proc)) {
            mrb->exc = NULL;
            proc = mrb_nil_value();
            script_bad++;
        } else {
            script_hits++;
        }
    }
    if (!mrb_proc_p(proc)) {
        mrb_ccontext* cxt = mrb_ccontext_new(mrb);
        mrb_ccontext_filename(mrb, cxt, name);
        cxt->capture_errors = TRUE;
        cxt->no_exec = TRUE;
        proc = mrb_load_nstring_cxt(mrb, RSTRING_PTR(code), (size_t)RSTRING_LEN(code), cxt);
        mrb_ccontext_free(mrb, cxt);
        if (mrb->exc) raise_pending(mrb);
        if (!mrb_proc_p(proc)) mrb_raisef(mrb, E_SCRIPT_ERROR, "%s: no code", name);
        script_misses++;
        uint8_t* out = NULL;
        size_t out_size = 0;
        if (mrb_dump_irep(mrb, mrb_proc_ptr(proc)->body.irep, MRB_DUMP_DEBUG_INFO, &out, &out_size) == MRB_DUMP_OK) {
            FILE* f = fopen(path, "wb");
            if (f) {
                int ok = fwrite(out, 1, out_size, f) == out_size;
                if (fclose(f) != 0) ok = 0;
                if (!ok) remove(path);
            }
            mrb_free(mrb, out);
        }
    }
    struct RProc* p = mrb_proc_ptr(proc);
    MRB_PROC_SET_TARGET_CLASS(p, mrb->object_class);
    if (mrb->c->ci) mrb_vm_ci_target_class_set(mrb->c->ci, mrb->object_class);
    mrb_gc_protect(mrb, proc);
    mrb_value result = mrb_top_run(mrb, p, mrb_top_self(mrb), 0);
    if (mrb->exc) raise_pending(mrb);
    return result;
}

/* TTM.script_cache_stats -> [loaded from bytecode, compiled, bad cache files] */
static mrb_value ttm_script_cache_stats(mrb_state* mrb, mrb_value self)
{
    mrb_value v[3] = { mrb_int_value(mrb, script_hits), mrb_int_value(mrb, script_misses),
                       mrb_int_value(mrb, script_bad) };
    return mrb_ary_new_from_values(mrb, 3, v);
}

static mrb_value ttm_crc32(mrb_state* mrb, mrb_value self)
{
    mrb_value s;
    mrb_get_args(mrb, "S", &s);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)RSTRING_PTR(s), (uInt)RSTRING_LEN(s));
    return mrb_int_value(mrb, (mrb_int)(uint32_t)crc);
}

static mrb_value ttm_inflate(mrb_state* mrb, mrb_value self)
{
    mrb_value src;
    mrb_get_args(mrb, "S", &src);
    z_stream zs = {0};
    if (inflateInit(&zs) != Z_OK) mrb_raise(mrb, E_RUNTIME_ERROR, "inflateInit failed");
    size_t cap = (size_t)RSTRING_LEN(src) * 4 + 256;
    mrb_value out = mrb_str_new(mrb, NULL, cap);
    zs.next_in = (Bytef*)RSTRING_PTR(src);
    zs.avail_in = (uInt)RSTRING_LEN(src);
    int rc;
    for (;;) {
        zs.next_out = (Bytef*)RSTRING_PTR(out) + zs.total_out;
        zs.avail_out = (uInt)(cap - zs.total_out);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) break;
        if (zs.avail_out == 0) {
            cap *= 2;
            mrb_str_resize(mrb, out, (mrb_int)cap);
        } else if (rc == Z_BUF_ERROR) {
            break;              /* input exhausted without a stream end */
        }
    }
    size_t total = zs.total_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) mrb_raise(mrb, E_RUNTIME_ERROR, "inflate: corrupt or truncated data");
    mrb_str_resize(mrb, out, (mrb_int)total);
    return out;
}

/* Array#include? in C. mruby inherits Enumerable#include?, written in Ruby on
 * top of each + a block; the game calls it per event per frame. Same
 * semantics: element == obj, re-reading the length every step. */
static mrb_value ary_include(mrb_state* mrb, mrb_value self)
{
    mrb_value obj;
    mrb_get_args(mrb, "o", &obj);
    for (mrb_int i = 0; i < RARRAY_LEN(self); ++i) {
        if (mrb_equal(mrb, mrb_ary_ref(mrb, self, i), obj)) return mrb_true_value();
    }
    return mrb_false_value();
}

void mrb_mruby_ttm_core_gem_init(mrb_state* mrb)
{
    mrb_define_method(mrb, mrb->array_class, "include?", ary_include, MRB_ARGS_REQ(1));

    struct RClass* ttm = mrb_define_module(mrb, "TTM");
    clock_base = raw_us();
    mrb_define_module_function(mrb, ttm, "us", ttm_us, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "reset_clock", ttm_reset_clock, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "obj_allocs", ttm_obj_allocs, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "gc_live", ttm_gc_live, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "read_file", ttm_read_file, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "inflate", ttm_inflate, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "load_script", ttm_load_script, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, ttm, "load_script_cached", ttm_load_script_cached, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, ttm, "script_cache_stats", ttm_script_cache_stats, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, ttm, "crc32", ttm_crc32, MRB_ARGS_REQ(1));
    ttm_init_archive(mrb, ttm);
    ttm_init_lang(mrb, ttm);
    ttm_init_fileio(mrb, ttm);
    ttm_init_marshal(mrb);
    ttm_init_profiler(mrb, ttm);
}

void mrb_mruby_ttm_core_gem_final(mrb_state* mrb)
{
    ttm_final_archive();
    ttm_final_lang();
}
