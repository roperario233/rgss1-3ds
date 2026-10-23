/*
 * backend_ndsp.c -- the 3DS's Audio backend: libctru's NDSP, on its own thread.
 *
 * NDSP channels 0-2 are the BGM, BGS and ME streams, each fed by NBUF wave
 * buffers of BUF_FRAMES frames (at 32 kHz, half a second queued ahead); 3-14
 * are the SE voices, each playing a cached sound straight from memory.
 *
 * Everything slow -- reading a sound from the pack, decoding, refilling
 * buffers, fades -- runs in au_service on the audio thread, woken by NDSP once
 * per DSP frame and when the game queues a command. The thread goes on core 2
 * (New 3DS), else core 1 (with the system's share capped at 30 %), else the
 * game's own core at a higher priority; the game's frames never wait for it.
 *
 * Memory the DSP reads must be linear, and the textures fill the linear heap
 * (bitmap.c evicts textures until a new one fits), so the sound's share is
 * taken at start-up: the stream buffers and a 4 MB pool for decoded sound
 * effects, handed out first-fit.
 *
 * The HOME menu and sleep are NDSP's own business (libctru's apt hook).
 * A console without the DSP firmware (sdmc:/3ds/dspfirm.cdc, dumped once by
 * the DSP1 homebrew) runs silent; the log says so.
 */

#ifdef __3DS__

#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "audio.h"

#define BUF_FRAMES 4096
#define NBUF 4
#define POOL_BYTES (4 * 1024 * 1024)
#define VOICE_CH(v) (AU_STREAMS + (v))

static int up;
static LightLock lock;
static LightEvent wake_ev;
static Thread thread;
static volatile int quit;
static int thread_core = -1;

static int16_t* sbuf[AU_STREAMS];
static ndspWaveBuf swb[AU_STREAMS][NBUF];
static ndspWaveBuf vwb[AU_VOICES];
static float mix_vol[AU_STREAMS], mix_rate[AU_STREAMS];
static u64 decode_ticks;

/* ---- the PCM pool: first fit over one linear block -------------------------------- */

typedef struct blk {
    uint32_t size;      /* bytes after the header */
    uint32_t free;
    struct blk* next;
    uint32_t pad;
} blk;

static uint8_t* pool;
static blk* pool_head;
static long pool_free_bytes;

static void pool_init(void)
{
    pool = (uint8_t*)linearAlloc(POOL_BYTES);
    if (!pool) return;
    pool_head = (blk*)pool;
    pool_head->size = POOL_BYTES - sizeof(blk);
    pool_head->free = 1;
    pool_head->next = NULL;
    pool_free_bytes = pool_head->size;
}

void* aub_pcm_alloc(size_t bytes)
{
    uint32_t need = (uint32_t)((bytes + 15) & ~(size_t)15);
    for (blk* b = pool_head; b; b = b->next) {
        if (!b->free || b->size < need) continue;
        if (b->size >= need + sizeof(blk) + 64) {    /* split */
            blk* rest = (blk*)((uint8_t*)(b + 1) + need);
            rest->size = b->size - need - (uint32_t)sizeof(blk);
            rest->free = 1;
            rest->next = b->next;
            b->next = rest;
            b->size = need;
            pool_free_bytes -= sizeof(blk);
        }
        b->free = 0;
        pool_free_bytes -= b->size;
        return b + 1;
    }
    return NULL;
}

void aub_pcm_free(void* p)
{
    if (!p) return;
    blk* b = (blk*)p - 1;
    b->free = 1;
    pool_free_bytes += b->size;
    for (blk* c = pool_head; c; c = c->next)    /* merge neighbours */
        while (c->free && c->next && c->next->free) {
            c->size += (uint32_t)sizeof(blk) + c->next->size;
            pool_free_bytes += sizeof(blk);
            c->next = c->next->next;
        }
}

long aub_pcm_free_kb(void) { return pool_free_bytes / 1024; }

/* ---- the thread ------------------------------------------------------------------------- */

void aub_lock(void) { LightLock_Lock(&lock); }
void aub_unlock(void) { LightLock_Unlock(&lock); }
void aub_wake(void) { LightEvent_Signal(&wake_ev); }
int aub_running(void) { return up; }

static void on_dsp_frame(void* arg)
{
    (void)arg;
    LightEvent_Signal(&wake_ev);
}

static void log_stats(u64 busy, u64 span)
{
    long kb = 0, n = 0;
    for (int i = 0; i < au_cache_n; ++i)
        if (au_cache[i].pcm) {
            kb += (long)(au_cache[i].bytes / 1024);
            n++;
        }
    au_log("audio: bgm %s, bgs %s; thread %.1f%% of core %d (decoding %.1f%%); SE cache %ld KB in %ld sounds "
           "(%ld decoded, %ld from cache), %ld KB free; %ld missing",
           au_streams[AU_BGM].name[0] ? au_streams[AU_BGM].name : "-",
           au_streams[AU_BGS].name[0] ? au_streams[AU_BGS].name : "-",
           100.0 * busy / span, thread_core, 100.0 * decode_ticks / span, kb, n,
           au_stat_se_loads, au_stat_se_hits, aub_pcm_free_kb(), au_stat_missing);
    decode_ticks = 0;
}

static void thread_main(void* arg)
{
    (void)arg;
    u64 last = svcGetSystemTick(), stat_start = last, busy = 0;
    const u64 ms = SYSCLOCK_ARM11 / 1000;
    while (!quit) {
        LightEvent_Wait(&wake_ev);
        if (quit) break;
        u64 t0 = svcGetSystemTick();
        u64 el = (t0 - last) / ms;
        last += el * ms;
        /* back from the HOME menu: fades carry on from where they were */
        au_service(el > 100 ? 100 : (int)el);
        busy += svcGetSystemTick() - t0;
        if (t0 - stat_start >= 30 * SYSCLOCK_ARM11) {
            log_stats(busy, t0 - stat_start);
            busy = 0;
            stat_start = t0;
        }
    }
}

int ttm_audio3ds_init(void)
{
    LightLock_Init(&lock);
    LightEvent_Init(&wake_ev, RESET_ONESHOT);
    Result r = ndspInit();
    if (R_FAILED(r)) {
        au_log("audio: no sound (ndspInit 0x%08lx) -- is sdmc:/3ds/dspfirm.cdc there? "
               "Run the DSP1 homebrew once to make it", (unsigned long)r);
        return 0;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int s = 0; s < AU_STREAMS; ++s) {
        sbuf[s] = (int16_t*)linearAlloc(NBUF * BUF_FRAMES * 2 * sizeof(int16_t));
        if (!sbuf[s]) {
            au_log("audio: no linear memory for the stream buffers");
            ndspExit();
            return 0;
        }
    }
    pool_init();
    if (!pool) au_log("audio: no linear memory for sound effects: SEs will be silent");
    up = 1;

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio = prio > 0x19 ? prio - 1 : 0x18;
    thread = threadCreate(thread_main, NULL, 64 * 1024, prio, 2, false);
    thread_core = 2;
    if (!thread) {
        APT_SetAppCpuTimeLimit(30);
        thread = threadCreate(thread_main, NULL, 64 * 1024, prio, 1, false);
        thread_core = 1;
    }
    if (!thread) {
        thread = threadCreate(thread_main, NULL, 64 * 1024, prio, -2, false);
        thread_core = 0;
    }
    if (!thread) {
        au_log("audio: could not start the audio thread");
        up = 0;
        ndspExit();
        return 0;
    }
    ndspSetCallback(on_dsp_frame, NULL);
    au_log("audio: NDSP up, thread on core %d, %d KB for sound effects", thread_core, pool ? POOL_BYTES / 1024 : 0);
    return 1;
}

void ttm_audio3ds_exit(void)
{
    if (!up) return;
    quit = 1;
    LightEvent_Signal(&wake_ev);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    ndspSetCallback(NULL, NULL);
    for (int c = 0; c < AU_STREAMS + AU_VOICES; ++c) ndspChnWaveBufClear(c);
    ndspExit();
    up = 0;
}

/* ---- streams ------------------------------------------------------------------------------ */

static void chn_setup(int ch, int channels, float rate)
{
    ndspChnWaveBufClear(ch);
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(ch, channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(ch, rate);
}

static void chn_volume(int ch, float vol)
{
    float m[12] = { 0 };
    m[0] = m[1] = vol;
    ndspChnSetMix(ch, m);
}

void aub_stream_start(int s)
{
    au_stream* st = &au_streams[s];
    chn_setup(s, st->dec.channels, (float)st->dec.rate);
    chn_volume(s, 0);
    mix_vol[s] = 0;
    mix_rate[s] = (float)st->dec.rate;
    memset(swb[s], 0, sizeof(swb[s]));
    if (s == AU_BGM)
        au_log("audio: bgm %s (%d Hz, %s)", st->name, st->dec.rate, st->dec.channels == 2 ? "stereo" : "mono");
}

void aub_stream_stop(int s)
{
    ndspChnWaveBufClear(s);
    memset(swb[s], 0, sizeof(swb[s]));
}

void aub_stream_mix(int s, float vol, float rate)
{
    if (vol != mix_vol[s]) {
        chn_volume(s, vol);
        mix_vol[s] = vol;
    }
    if (rate != mix_rate[s]) {
        ndspChnSetRate(s, rate);
        mix_rate[s] = rate;
    }
}

void aub_stream_pause(int s, int paused) { ndspChnSetPaused(s, paused); }

void aub_stream_pump(int s, int elapsed_ms)
{
    (void)elapsed_ms;
    au_stream* st = &au_streams[s];
    int ch = st->dec.channels;
    for (int b = 0; b < NBUF && !st->eof; ++b) {
        ndspWaveBuf* wb = &swb[s][b];
        if (wb->status != NDSP_WBUF_FREE && wb->status != NDSP_WBUF_DONE) continue;
        int16_t* data = sbuf[s] + (size_t)b * BUF_FRAMES * 2;
        u64 t0 = svcGetSystemTick();
        int n = au_stream_read(st, data, BUF_FRAMES);
        decode_ticks += svcGetSystemTick() - t0;
        if (n <= 0) break;
        memset(wb, 0, sizeof(*wb));
        wb->data_pcm16 = data;
        wb->nsamples = (u32)n;
        DSP_FlushDataCache(data, (u32)n * ch * sizeof(int16_t));
        ndspChnWaveBufAdd(s, wb);
    }
}

int aub_stream_drained(int s)
{
    if (!au_streams[s].eof) return 0;
    for (int b = 0; b < NBUF; ++b)
        if (swb[s][b].status == NDSP_WBUF_QUEUED || swb[s][b].status == NDSP_WBUF_PLAYING) return 0;
    return 1;
}

/* ---- voices ------------------------------------------------------------------------------ */

void aub_voice_play(int v)
{
    const au_se* se = au_voice_se(v);
    if (!se || !se->frames) return;
    int ch = VOICE_CH(v);
    chn_setup(ch, se->channels, se->rate * au_voices[v].pitch / 100.0f);
    chn_volume(ch, au_voices[v].vol);
    memset(&vwb[v], 0, sizeof(vwb[v]));
    vwb[v].data_pcm16 = se->pcm;
    vwb[v].nsamples = (u32)se->frames;
    DSP_FlushDataCache(se->pcm, (u32)(se->frames * se->channels * sizeof(int16_t)));
    ndspChnWaveBufAdd(ch, &vwb[v]);
}

void aub_voice_stop(int v)
{
    ndspChnWaveBufClear(VOICE_CH(v));
    memset(&vwb[v], 0, sizeof(vwb[v]));
}

int aub_voice_busy(int v)
{
    return vwb[v].status == NDSP_WBUF_QUEUED || vwb[v].status == NDSP_WBUF_PLAYING;
}

void aub_tick(int elapsed_ms) { (void)elapsed_ms; }

#endif /* __3DS__ */
