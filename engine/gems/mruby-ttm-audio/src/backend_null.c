/*
 * backend_null.c -- the PC's Audio backend: no sound card.
 *
 * Commands run at once (aub_wake calls au_service), and time moves only when
 * the caller says so (Audio.ttm_advance(ms): pc/audio_check.rb, and pc/run_pc.rb
 * once a frame). Streams are really decoded as their time goes by, at their
 * rate times their pitch, so looping, the end of an ME and fades behave as on
 * the console; voices just count their frames. SE memory has the console's
 * budget (4 MB), so the cache gives sounds up the same way.
 */

#ifndef __3DS__

#include <stdlib.h>

#include "audio.h"

#define PCM_BUDGET (4 * 1024 * 1024)

static float rate_of[AU_STREAMS];
static double owed[AU_STREAMS];
static int voice_on[AU_VOICES];
static double voice_pos[AU_VOICES];
static long pcm_used;
static int16_t scratch[2 * 4096];

void aub_lock(void) {}
void aub_unlock(void) {}
void aub_wake(void) { au_service(0); }
int aub_running(void) { return 1; }

void aub_stream_start(int s)
{
    rate_of[s] = (float)au_streams[s].dec.rate;
    owed[s] = 0;
}
void aub_stream_stop(int s) { owed[s] = 0; }
void aub_stream_mix(int s, float vol, float rate)
{
    (void)vol;
    rate_of[s] = rate;
}
void aub_stream_pause(int s, int paused) { (void)s; (void)paused; }

void aub_stream_pump(int s, int elapsed_ms)
{
    au_stream* st = &au_streams[s];
    owed[s] += elapsed_ms * (double)rate_of[s] / 1000.0;
    while (owed[s] >= 1 && !st->eof) {
        int want = owed[s] > 4096 ? 4096 : (int)owed[s];
        int got = au_stream_read(st, scratch, want);
        owed[s] -= got;
        if (got < want) break;
    }
}

int aub_stream_drained(int s) { return au_streams[s].eof; }

void aub_voice_play(int v)
{
    voice_on[v] = 1;
    voice_pos[v] = 0;
}
void aub_voice_stop(int v) { voice_on[v] = 0; }

int aub_voice_busy(int v)
{
    const au_se* se = au_voice_se(v);
    return voice_on[v] && se && voice_pos[v] < (double)se->frames;
}

void aub_tick(int elapsed_ms)
{
    for (int v = 0; v < AU_VOICES; ++v) {
        const au_se* se = au_voice_se(v);
        if (!voice_on[v] || !se) continue;
        voice_pos[v] += elapsed_ms * (double)se->rate * au_voices[v].pitch / 100.0 / 1000.0;
        au_voices[v].pos = (uint64_t)voice_pos[v];
        if (voice_pos[v] >= (double)se->frames) voice_on[v] = 0;
    }
}

/* each block carries its size in front, for the budget */
void* aub_pcm_alloc(size_t bytes)
{
    if (pcm_used + (long)bytes > PCM_BUDGET) return NULL;
    size_t* p = (size_t*)malloc(bytes + sizeof(size_t) * 2);
    if (!p) return NULL;
    p[0] = bytes;
    pcm_used += (long)bytes;
    return p + 2;
}

void aub_pcm_free(void* ptr)
{
    if (!ptr) return;
    size_t* p = (size_t*)ptr - 2;
    pcm_used -= (long)p[0];
    free(p);
}

long aub_pcm_free_kb(void) { return (PCM_BUDGET - pcm_used) / 1024; }

#endif /* !__3DS__ */
