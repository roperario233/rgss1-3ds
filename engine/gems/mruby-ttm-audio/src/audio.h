/*
 * audio.h -- RGSS1's Audio module for the To the Moon 3DS engine.
 *
 * One shared core (audio.c: what the game asked for, fades, BGM under ME, the
 * SE cache, files) and two backends that play it:
 *   backend_ndsp.c  the 3DS: the DSP through libctru's NDSP, on its own thread
 *   backend_null.c  the PC: no sound card; time is advanced by Audio.ttm_advance
 *                   and the streams really are decoded, so the PC checks test
 *                   the console's decoding, looping and fades
 * Sounds come from the audio pack tools/pack_audio.py makes (one file of Ogg
 * Vorbis streams), or from the game's own Audio/ folder; decoding is
 * stb_vorbis on both machines.
 */

#ifndef TTM_AUDIO_H
#define TTM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

/* streams (one NDSP channel each), then the SE voices */
enum { AU_BGM, AU_BGS, AU_ME, AU_STREAMS };
#define AU_VOICES 12
#define AU_NAME 128

/* a decoder over a whole file in memory: Ogg Vorbis, or 16-bit PCM WAV */
typedef struct {
    uint8_t* data;
    size_t size;
    stb_vorbis* vorbis;
    const int16_t* wav;          /* WAV: the samples, inside data */
    size_t wav_frames, wav_pos;
    int channels, rate;          /* channels 1 or 2 */
} au_dec;

typedef struct {
    char name[AU_NAME];          /* the key ("bgm/name"), "" when stopped */
    au_dec dec;
    int open;                    /* dec holds a file */
    int playing;                 /* started and not stopped / finished */
    int paused;                  /* BGM, while an ME plays */
    int loop;                    /* BGM and BGS loop the whole file; ME does not */
    int eof;                     /* the decoder reached the end (not looping) */
    float vol;                   /* the game's volume, 0..1 */
    int pitch;                   /* percent */
    float fade;                  /* 1 -> 0 during a fade-out */
    int fade_ms, fade_left;      /* fading out while fade_left > 0 */
    float duck;                  /* BGM under an ME: 0 (silent) .. 1 */
    int duck_dir;                /* -1 going quiet, +1 coming back, 0 still */
    uint64_t frames;             /* frames handed to the backend so far */
} au_stream;

/* a sound effect, decoded once and kept (the message system plays one per letter) */
typedef struct {
    char name[AU_NAME];
    int16_t* pcm;                /* in the backend's PCM memory (linear on the 3DS) */
    size_t frames, bytes;
    int channels, rate;
    uint32_t last_used;
} au_se;

typedef struct {
    int se;                      /* index into the cache, -1 when free */
    float vol;
    int pitch;
    uint32_t started;            /* the newest voice has the largest */
    uint64_t pos;                /* the PC's playback position */
} au_voice;

extern au_stream au_streams[AU_STREAMS];
extern au_voice au_voices[AU_VOICES];
extern au_se* au_cache;
extern int au_cache_n;

/* core (audio.c) */
/* up to `frames` frames of s into out (interleaved, s->dec.channels), looping
 * if s->loop; fewer only at the end of a sound that does not loop */
int au_stream_read(au_stream* s, int16_t* out, int frames);
/* drain the game's commands and advance fades by elapsed_ms (backend thread) */
void au_service(int elapsed_ms);
void au_log(const char* fmt, ...);
const au_se* au_voice_se(int v);         /* the sound voice v plays, or NULL */
const char* au_stream_name(int i);       /* "bgm", "bgs", "me" */
/* where log lines go (the 3DS app's log.txt); NULL = stdout */
extern void (*ttm_audio_log_fn)(const char* line);
/* figures for the backend's periodic log line */
extern long au_stat_se_loads, au_stat_se_hits, au_stat_missing;

/* backend */
void aub_lock(void);
void aub_unlock(void);
void aub_wake(void);                              /* commands are waiting */
int  aub_running(void);                           /* 0: no device -- commands are dropped */
void aub_stream_start(int s);                     /* configure for au_streams[s].dec and start */
void aub_stream_stop(int s);
void aub_stream_mix(int s, float vol, float rate);
void aub_stream_pause(int s, int paused);
void aub_stream_pump(int s, int elapsed_ms);      /* keep it fed (3DS) / consume (PC) */
int  aub_stream_drained(int s);                   /* eof and everything queued has played */
void aub_voice_play(int v);
void aub_voice_stop(int v);
int  aub_voice_busy(int v);
void aub_tick(int elapsed_ms);                    /* PC: advance voices */
void* aub_pcm_alloc(size_t bytes);                /* NULL when full */
void aub_pcm_free(void* p);
long aub_pcm_free_kb(void);

#endif
