/*
 * profiler.c -- line-level sampling profiler for Ruby code (PC build only).
 *
 * Needs MRB_USE_DEBUG_HOOK (build_config_3ds.rb sets it for the host build;
 * the 3DS build leaves it off, so this file compiles to nothing there).
 *
 * A background thread raises a flag every `interval` microseconds; mruby's
 * code-fetch hook sees it at the next instruction and records the current
 * script file, line and method. Time spent inside a C method is charged to the
 * Ruby line that called it, because the next instruction fetched is there.
 *
 * TTM.profile_start(interval_us = 50)
 * TTM.profile_stop -> { "file:line method" => samples, ... }
 */

#include <mruby.h>

#ifdef MRB_USE_DEBUG_HOOK

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mruby/debug.h>
#include <mruby/hash.h>
#include <mruby/irep.h>
#include <mruby/string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

typedef struct {
    const mrb_irep* irep;
    int32_t line;
    uint32_t count;
    char* label;          /* "file:line method", made on first sample */
} slot_t;

#define SLOTS 65536
static slot_t* slots;
static volatile int tick;
static volatile int running;
static unsigned interval_us = 50;

#ifdef _WIN32
static HANDLE thread;
static DWORD WINAPI ticker(LPVOID arg)
{
    LARGE_INTEGER f, last, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&last);
    LONGLONG step = f.QuadPart * interval_us / 1000000;
    while (running) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart - last.QuadPart >= step) {
            tick = 1;
            last = now;
        }
    }
    return 0;
}
#else
static pthread_t thread;
static void* ticker(void* arg)
{
    struct timespec ts = { 0, (long)interval_us * 1000 };
    while (running) {
        nanosleep(&ts, NULL);
        tick = 1;
    }
    return NULL;
}
#endif

static void record(mrb_state* mrb, const mrb_irep* irep, const mrb_code* pc)
{
    uint32_t off = (uint32_t)(pc - irep->iseq);
    int32_t line = mrb_debug_get_line(mrb, irep, off);
    uintptr_t h = ((uintptr_t)irep >> 4) * 2654435761u ^ (uintptr_t)line * 40503u;
    for (uint32_t i = 0; i < SLOTS; ++i) {
        slot_t* s = &slots[(h + i) & (SLOTS - 1)];
        if (s->irep == irep && s->line == line) {
            s->count++;
            return;
        }
        if (s->irep == NULL) {
            s->irep = irep;
            s->line = line;
            s->count = 1;
            const char* file = mrb_debug_get_filename(mrb, irep, off);
            const char* meth = mrb->c->ci->mid ? mrb_sym_name(mrb, mrb->c->ci->mid) : "(top)";
            char buf[256];
            snprintf(buf, sizeof(buf), "%s:%d %s", file ? file : "?", (int)line, meth ? meth : "?");
            s->label = (char*)malloc(strlen(buf) + 1);
            strcpy(s->label, buf);
            return;
        }
    }
}

static void hook(mrb_state* mrb, const mrb_irep* irep, const mrb_code* pc, mrb_value* regs)
{
    if (!tick) return;
    tick = 0;
    record(mrb, irep, pc);
}

static mrb_value profile_start(mrb_state* mrb, mrb_value self)
{
    mrb_int iv = 50;
    mrb_get_args(mrb, "|i", &iv);
    if (running) return mrb_false_value();
    interval_us = iv > 0 ? (unsigned)iv : 50;
    if (!slots) slots = (slot_t*)calloc(SLOTS, sizeof(slot_t));
    tick = 0;
    running = 1;
#ifdef _WIN32
    thread = CreateThread(NULL, 0, ticker, NULL, 0, NULL);
#else
    pthread_create(&thread, NULL, ticker, NULL);
#endif
    mrb->code_fetch_hook = hook;
    return mrb_true_value();
}

static mrb_value profile_stop(mrb_state* mrb, mrb_value self)
{
    if (!running) return mrb_nil_value();
    running = 0;
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    mrb->code_fetch_hook = NULL;
    mrb_value h = mrb_hash_new(mrb);
    for (uint32_t i = 0; i < SLOTS; ++i) {
        slot_t* s = &slots[i];
        if (!s->irep) continue;
        mrb_hash_set(mrb, h, mrb_str_new_cstr(mrb, s->label), mrb_int_value(mrb, (mrb_int)s->count));
        free(s->label);
        memset(s, 0, sizeof(*s));
    }
    return h;
}

void ttm_init_profiler(mrb_state* mrb, struct RClass* ttm)
{
    mrb_define_module_function(mrb, ttm, "profile_start", profile_start, MRB_ARGS_OPT(1));
    mrb_define_module_function(mrb, ttm, "profile_stop", profile_stop, MRB_ARGS_NONE());
}

#else  /* no debug hook: no profiler (the 3DS build) */

void ttm_init_profiler(mrb_state* mrb, struct RClass* ttm) { (void)mrb; (void)ttm; }

#endif
