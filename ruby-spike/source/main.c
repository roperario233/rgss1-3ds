/*
 * ruby_spike -- M2 Ruby-host spike for the To the Moon 3DS port.
 *
 * Question it answers: can mruby 4.0 host the game's Ruby on a New 3DS, and at
 * what cost? It embeds mruby (built by build_mruby.ps1) and compiles a test
 * script ON THE DEVICE, the way the port will have to compile the game's
 * scripts from the player's own data. Two scripts, picked from a menu:
 *   part 1  romfs:/spike.rb   language checks + micro-benchmarks
 *   part 2  romfs:/spike2.rb  frame times under the GC at the game's live-set
 *                             size (takes a couple of minutes)
 *   part 3  romfs:/part3.rb   the GAME'S OWN SCRIPTS, headless: reads the
 *                             player's To the Moon.rgssad from the SD card,
 *                             runs the scripts and times real map frames
 *
 * Reported on screen and in sdmc:/3ds/ttm3ds_ruby/result_part<N>.txt:
 *   top screen     the script's own lines
 *   bottom screen  mrb_open cost, parse / codegen / run times, the script's
 *                  summary, and memory
 *
 * Memory: mruby's own bytes (current and peak) are counted exactly by the
 * mrb_basic_alloc_func() below; mallinfo().uordblks gives the whole process.
 * (devkitARM's newlib leaves mallinfo().usmblks, the peak, at zero.)
 *
 * Controls: X = part 3, A = part 2, Y = part 1, START = exit.
 */

#include <3ds.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/version.h>

/* The mruby parser and code generator recurse on the C stack; libctru's default
 * 32 KB main-thread stack is not enough for a 20 KB script. */
u32 __stacksize__ = 1024 * 1024;

#define LOG_DIR  "sdmc:/3ds/ttm3ds_ruby"

static PrintConsole top_con, bot_con;
static FILE* log_file;
static const char* cpu_clock = "?";

/* Time is measured from the start of each run, so microsecond counts stay
 * inside mruby's 31-bit immediate integers (~17 minutes). */
static u64 tick_base;

/* devkitARM's newlib declares C11 timespec_get() (and TIME_UTC) but does not
 * implement it; mruby-time calls it for Time.now. libctru does implement
 * gettimeofday(), so build it on that. */
int timespec_get(struct timespec* ts, int base)
{
    struct timeval tv;
    if (base != TIME_UTC || gettimeofday(&tv, NULL) != 0) return 0;
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = (long)tv.tv_usec * 1000;
    return base;
}

/* the system tick runs at a fixed 268 MHz whatever the CPU clock mode */
static double now_ms(void)
{
    return (double)(svcGetSystemTick() - tick_base) / (SYSCLOCK_ARM11 / 1000.0);
}

static u32 now_us(void)
{
    return (u32)((svcGetSystemTick() - tick_base) * 1000000ULL / SYSCLOCK_ARM11);
}

static size_t heap_used(void) { return mallinfo().uordblks; }

/* Every mruby allocation goes through mrb_basic_alloc_func(); defining it here
 * replaces libmruby's default (allocf.c holds nothing else, so the archive copy
 * is never pulled in). A size header lets it count mruby's bytes exactly. */
typedef struct { size_t size; size_t pad; } alloc_hdr;  /* 8 bytes: keeps 8-byte alignment */
static size_t mrb_bytes, mrb_bytes_peak;

void* mrb_basic_alloc_func(void* p, size_t size)
{
    alloc_hdr* h = p ? (alloc_hdr*)p - 1 : NULL;
    if (size == 0) {
        if (h) {
            mrb_bytes -= h->size;
            free(h);
        }
        return NULL;
    }
    size_t old = h ? h->size : 0;
    alloc_hdr* n = (alloc_hdr*)realloc(h, sizeof(alloc_hdr) + size);
    if (!n) return NULL;
    n->size = size;
    mrb_bytes = mrb_bytes - old + size;
    if (mrb_bytes > mrb_bytes_peak) mrb_bytes_peak = mrb_bytes;
    return n + 1;
}

static void present(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/* one line to a console and to the log file */
static void put_line(PrintConsole* con, const char* s, int n)
{
    consoleSelect(con);
    printf("%.*s\n", n, s);
    if (log_file) fprintf(log_file, "%.*s\n", n, s);
    present();
}

static void bot(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    put_line(&bot_con, buf, n);
}

/* ------------------------------------------------------------------ */
/* the Spike module the script reports through                         */
/* ------------------------------------------------------------------ */

static mrb_value spike_log(mrb_state* mrb, mrb_value self)
{
    const char* s;
    mrb_int n;
    mrb_get_args(mrb, "s", &s, &n);
    put_line(&top_con, s, (int)n);
    return mrb_nil_value();
}

static mrb_value spike_ms(mrb_state* mrb, mrb_value self)
{
    return mrb_float_value(mrb, now_ms());
}

/* integer microseconds: unlike Spike.ms, creates no Float garbage */
static mrb_value spike_us(mrb_state* mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)now_us());
}

/* the GC's own count of live objects */
static mrb_value spike_gc_live(mrb_state* mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)mrb->gc.live);
}

/* mruby's own live bytes, exact */
static mrb_value spike_heap_used(mrb_state* mrb, mrb_value self)
{
    return mrb_int_value(mrb, (mrb_int)mrb_bytes);
}

static mrb_value spike_platform(mrb_state* mrb, mrb_value self)
{
    return mrb_str_new_cstr(mrb, "3ds");
}

static void report_exception(mrb_state* mrb)
{
    mrb_value exc = mrb_obj_value(mrb->exc);
    mrb->exc = NULL;
    mrb_value msg = mrb_inspect(mrb, exc);
    bot("ERROR %.*s", (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
    mrb_value bt = mrb_funcall(mrb, exc, "backtrace", 0);
    if (mrb_array_p(bt)) {
        for (mrb_int i = 0; i < RARRAY_LEN(bt) && i < 4; ++i) {
            mrb_value line = mrb_ary_entry(bt, i);
            if (mrb_string_p(line))
                bot("  %.*s", (int)RSTRING_LEN(line), RSTRING_PTR(line));
        }
    }
    mrb->exc = NULL;
}

static char* read_file(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = n > 0 ? (char*)malloc((size_t)n + 1) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

/* ------------------------------------------------------------------ */
/* the spike                                                           */
/* ------------------------------------------------------------------ */

static void clear_screens(void)
{
    consoleSelect(&top_con);
    consoleClear();
    consoleSelect(&bot_con);
    consoleClear();
}

/* Runs one script in a fresh mrb_state, logging to log_path. */
static void run_spike(const char* name, const char* script, const char* log_path)
{
    clear_screens();
    log_file = fopen(log_path, "w");
    tick_base = svcGetSystemTick();
    mrb_bytes_peak = mrb_bytes;

    bot("M2 Ruby spike %s: mruby %s", name, MRUBY_VERSION);
    bot("CPU %s", cpu_clock);
    bot("heap %u KB, linear %u KB", (unsigned)(envGetHeapSize() / 1024),
        (unsigned)(envGetLinearHeapSize() / 1024));

    size_t src_len = 0;
    char* src = read_file(script, &src_len);
    if (!src) {
        bot("cannot read %s", script);
        goto done;
    }

    /* 1. interpreter start-up */
    size_t h0 = heap_used();
    double t0 = now_ms();
    mrb_state* mrb = mrb_open();
    double t1 = now_ms();
    if (!mrb) {
        bot("mrb_open failed (out of memory?)");
        free(src);
        goto done;
    }
    size_t h1 = heap_used();
    bot("mrb_open %7.1f ms  +%u KB", t1 - t0, (unsigned)((h1 - h0) / 1024));

    struct RClass* spike = mrb_define_module(mrb, "Spike");
    mrb_define_module_function(mrb, spike, "log", spike_log, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, spike, "ms", spike_ms, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, spike, "heap_used", spike_heap_used, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, spike, "platform", spike_platform, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, spike, "us", spike_us, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, spike, "gc_live", spike_gc_live, MRB_ARGS_NONE());

    /* 2. compile on the device, parse and codegen timed separately */
    mrb_ccontext* cxt = mrb_ccontext_new(mrb);
    mrb_ccontext_filename(mrb, cxt, strrchr(script, '/') + 1);
    cxt->capture_errors = TRUE;

    double t2 = now_ms();
    struct mrb_parser_state* p = mrb_parse_nstring(mrb, src, src_len, cxt);
    double t3 = now_ms();
    free(src);
    if (!p || !p->tree || p->nerr > 0) {
        if (p) {
            bot("SYNTAX line %d: %s", (int)p->error_buffer[0].lineno,
                p->error_buffer[0].message ? p->error_buffer[0].message : "?");
            mrb_parser_free(p);
        } else {
            bot("parser could not start");
        }
        mrb_ccontext_free(mrb, cxt);
        mrb_close(mrb);
        goto done;
    }
    struct RProc* proc = mrb_generate_code(mrb, p);
    double t4 = now_ms();
    mrb_parser_free(p);
    mrb_ccontext_free(mrb, cxt);
    if (!proc) {
        bot("codegen failed");
        mrb_close(mrb);
        goto done;
    }
    bot("parse    %7.1f ms  (%u KB src)", t3 - t2, (unsigned)(src_len / 1024));
    bot("codegen  %7.1f ms", t4 - t3);

    /* 3. run it (same steps as mrb_load_exec) */
    MRB_PROC_SET_TARGET_CLASS(proc, mrb->object_class);
    mrb_top_run(mrb, proc, mrb_top_self(mrb), 0);
    double t5 = now_ms();
    bot("run      %7.1f ms", t5 - t4);
    if (mrb->exc) report_exception(mrb);

    /* 4. the script's own summary */
    mrb_value summary = mrb_gv_get(mrb, mrb_intern_lit(mrb, "$spike_summary"));
    if (mrb_array_p(summary)) {
        for (mrb_int i = 0; i < RARRAY_LEN(summary); ++i) {
            mrb_value line = mrb_ary_entry(summary, i);
            if (mrb_string_p(line))
                bot("%.*s", (int)RSTRING_LEN(line), RSTRING_PTR(line));
        }
    }

    /* 5. memory */
    size_t h_run = heap_used();
    mrb_full_gc(mrb);
    size_t h_gc = heap_used();
    bot("heap in use  %5u KB (%u after GC)", (unsigned)((h_run - h0) / 1024),
        (unsigned)((h_gc - h0) / 1024));
    bot("mruby peak   %5u KB of %u KB heap", (unsigned)(mrb_bytes_peak / 1024),
        (unsigned)(envGetHeapSize() / 1024));
    mrb_close(mrb);
    long leak = (long)heap_used() - (long)h0;
    bot("after close  %+5ld KB (mruby %u B)", leak / 1024, (unsigned)mrb_bytes);

done:
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
        bot("saved %s", log_path);
    } else {
        bot("(could not write %s)", log_path);
    }
}

static void show_menu(void)
{
    bot("");
    bot("X: part 3  the game's scripts (~3 min)");
    bot("   needs To the Moon.rgssad in");
    bot("   " LOG_DIR "/");
    bot("A: part 2  frame times + GC (~2 min)");
    bot("Y: part 1  language checks (~5 s)");
    bot("START: exit");
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, &top_con);
    consoleInit(GFX_BOTTOM, &bot_con);
    romfsInit();

    mkdir("sdmc:/3ds", 0777);
    mkdir(LOG_DIR, 0777);

    /* New 3DS: ask for 804 MHz + L2 directly so the result says whether it
     * took effect (osSetSpeedupEnable would fail silently). */
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    cpu_clock = "268 MHz (Old 3DS)";
    if (n3ds) {
        cpu_clock = "268 MHz (boost refused)";
        if (R_SUCCEEDED(ptmSysmInit())) {
            if (R_SUCCEEDED(PTMSYSM_ConfigureNew3DSCPU(3)))
                cpu_clock = "804 MHz + L2";
            ptmSysmExit();
        }
    }

    bot("M2 Ruby spike: mruby %s", MRUBY_VERSION);
    bot("CPU %s", cpu_clock);
    show_menu();

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_X) {
            run_spike("part 3", "romfs:/part3.rb", LOG_DIR "/result_part3.txt");
            show_menu();
        } else if (down & KEY_A) {
            run_spike("part 2", "romfs:/spike2.rb", LOG_DIR "/result_part2.txt");
            show_menu();
        } else if (down & KEY_Y) {
            run_spike("part 1", "romfs:/spike.rb", LOG_DIR "/result_part1.txt");
            show_menu();
        }
        present();
        gspWaitForVBlank();
    }

    romfsExit();
    gfxExit();
    return 0;
}
