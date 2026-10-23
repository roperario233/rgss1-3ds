/*
 * audio.c -- the shared core of RGSS1's Audio module (see audio.h).
 *
 * The game's calls (Audio.bgm_play and friends) only queue a command, so a
 * call never stalls a frame; the backend's thread (the PC: the call itself)
 * runs au_service, which loads, decodes and plays. What the game sees follows
 * RGSS / mkxp:
 *  - bgm_play / bgs_play of the sound already playing, at the same pitch,
 *    only changes its volume (the game calls it on every map change);
 *  - BGM and BGS loop the whole file (no To the Moon file has loop tags); an
 *    ME plays once, the BGM goes quiet under it and fades back in over a
 *    second when it ends;
 *  - *_fade(ms) fades out linearly, then stops;
 *  - volume is linear (volume / 100), pitch is a playback rate (pitch / 100);
 *  - SEs play on AU_VOICES voices, the oldest giving way when all are busy;
 *    each SE is decoded once and kept (least recently used ones go when the
 *    backend's PCM memory is full) -- the message system plays one per letter.
 * A sound that cannot be found is logged once and stays silent: RGSS would
 * raise, but no missing sound should end the game.
 *
 * Where sounds come from: the pack (tools/pack_audio.py: "TTMA", an index of
 * "bgm/name" keys, then Ogg Vorbis streams), else the game folder's own
 * Audio/<kind>/<name>.ogg or .wav.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include "audio.h"

au_stream au_streams[AU_STREAMS];
au_voice au_voices[AU_VOICES];
au_se* au_cache;
int au_cache_n;
long au_stat_se_loads, au_stat_se_hits, au_stat_missing;
void (*ttm_audio_log_fn)(const char* line);

static const char* const STREAM_NAMES[AU_STREAMS] = { "bgm", "bgs", "me" };
#define AU_CACHE_CAP 512

void au_log(const char* fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (ttm_audio_log_fn) ttm_audio_log_fn(line);
    else printf("%s\n", line);
}

/* ---- the pack -------------------------------------------------------------------- */

typedef struct {
    char* key;
    uint32_t offset, length, rate, frames;
    int channels;
} pack_entry;

static FILE* pack_file;
static pack_entry* pack;
static int pack_n;
static char game_dir[256];

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int pack_open(const char* path)
{
    if (pack_file) fclose(pack_file);
    for (int i = 0; i < pack_n; ++i) free(pack[i].key);
    free(pack);
    pack = NULL;
    pack_n = 0;
    pack_file = path && *path ? fopen(path, "rb") : NULL;
    if (!pack_file) return 0;
    uint8_t h[16];
    if (fread(h, 1, 16, pack_file) != 16 || memcmp(h, "TTMA", 4) != 0 || rd32(h + 4) != 1) {
        au_log("audio: %s is not an audio pack (tools/pack_audio.py makes one)", path);
        fclose(pack_file);
        pack_file = NULL;
        return 0;
    }
    uint32_t count = rd32(h + 8), bytes = rd32(h + 12);
    uint8_t* index = (uint8_t*)malloc(bytes);
    pack = (pack_entry*)calloc(count ? count : 1, sizeof(pack_entry));
    if (!index || !pack || fread(index, 1, bytes, pack_file) != bytes) {
        free(index);
        free(pack);
        pack = NULL;
        fclose(pack_file);
        pack_file = NULL;
        au_log("audio: %s: cannot read its index", path);
        return 0;
    }
    const uint8_t* p = index;
    for (uint32_t i = 0; i < count && p + 2 <= index + bytes; ++i) {
        int n = p[0] | p[1] << 8;
        pack_entry* e = &pack[pack_n++];
        e->key = (char*)malloc((size_t)n + 1);
        memcpy(e->key, p + 2, (size_t)n);
        e->key[n] = 0;
        p += 2 + n;
        e->offset = rd32(p);
        e->length = rd32(p + 4);
        e->channels = p[8];
        e->rate = rd32(p + 12);
        e->frames = rd32(p + 16);
        p += 20;
    }
    free(index);
    return pack_n;
}

/* the pack is sorted by key, byte order (tools/pack_audio.py) */
static pack_entry* pack_find(const char* key)
{
    int lo = 0, hi = pack_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(key, pack[mid].key);
        if (c == 0) return &pack[mid];
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

/* "Audio/BGM/Name.ogg" (or with backslashes) -> "bgm/name"; the path as the
 * game wrote it, from "BGM/...", goes to rel for the loose-file fallback */
static void make_key(const char* name, char key[AU_NAME], char rel[AU_NAME])
{
    char tmp[AU_NAME];
    snprintf(tmp, sizeof(tmp), "%s", name);
    for (char* c = tmp; *c; ++c)
        if (*c == '\\') *c = '/';
    const char* s = tmp;
    if (strncasecmp(s, "audio/", 6) == 0) s += 6;
    snprintf(rel, AU_NAME, "%s", s);
    char* dot = strrchr(rel, '.');
    if (dot && !strchr(dot, '/') &&
        (!strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".wav") || !strcasecmp(dot, ".mp3") ||
         !strcasecmp(dot, ".wma") || !strcasecmp(dot, ".mid")))
        *dot = 0;
    size_t i = 0;
    for (; rel[i] && i < AU_NAME - 1; ++i) {
        unsigned char c = (unsigned char)rel[i];
        key[i] = c < 0x80 ? (char)tolower(c) : (char)c;
    }
    key[i] = 0;
}

/* each missing sound is logged once */
static char* missing[128];
static int missing_n;

static void note_missing(const char* key, const char* why)
{
    au_stat_missing++;
    for (int i = 0; i < missing_n; ++i)
        if (!strcmp(missing[i], key)) return;
    if (missing_n < 128) missing[missing_n++] = strdup(key);
    au_log("audio: %s %s", key, why);
}

/* ---- decoders ---------------------------------------------------------------------- */

static void dec_close(au_dec* d)
{
    if (d->vorbis) stb_vorbis_close(d->vorbis);
    free(d->data);
    memset(d, 0, sizeof(*d));
}

/* a 16-bit PCM WAV, mono or stereo */
static int wav_open(au_dec* d)
{
    const uint8_t* p = d->data;
    size_t n = d->size, at = 12;
    int fmt_ok = 0;
    if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    while (at + 8 <= n) {
        uint32_t len = rd32(p + at + 4);
        const uint8_t* body = p + at + 8;
        if (!memcmp(p + at, "fmt ", 4) && len >= 16) {
            int tag = body[0] | body[1] << 8, bits = body[14] | body[15] << 8;
            d->channels = body[2] | body[3] << 8;
            d->rate = (int)rd32(body + 4);
            fmt_ok = tag == 1 && bits == 16 && (d->channels == 1 || d->channels == 2);
        } else if (!memcmp(p + at, "data", 4) && fmt_ok) {
            if (len > n - at - 8) len = (uint32_t)(n - at - 8);
            d->wav = (const int16_t*)body;
            d->wav_frames = len / (2u * (unsigned)d->channels);
            return 1;
        }
        at += 8 + len + (len & 1);
    }
    return 0;
}

/* d->data / d->size hold a whole file: start decoding it */
static int dec_open(au_dec* d)
{
    if (d->size >= 4 && !memcmp(d->data, "OggS", 4)) {
        int err = 0;
        d->vorbis = stb_vorbis_open_memory(d->data, (int)d->size, &err, NULL);
        if (!d->vorbis) return 0;
        stb_vorbis_info info = stb_vorbis_get_info(d->vorbis);
        d->channels = info.channels >= 2 ? 2 : 1;
        d->rate = (int)info.sample_rate;
        return 1;
    }
    return wav_open(d);
}

static int dec_read(au_dec* d, int16_t* out, int frames)
{
    if (d->vorbis)
        return stb_vorbis_get_samples_short_interleaved(d->vorbis, d->channels, out, frames * d->channels);
    size_t left = d->wav_frames - d->wav_pos;
    if ((size_t)frames > left) frames = (int)left;
    memcpy(out, d->wav + d->wav_pos * d->channels, (size_t)frames * d->channels * 2);
    d->wav_pos += (size_t)frames;
    return frames;
}

static void dec_rewind(au_dec* d)
{
    if (d->vorbis) stb_vorbis_seek_start(d->vorbis);
    else d->wav_pos = 0;
}

static uint8_t* read_whole(const char* path, size_t* size)
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

/* a sound by its key: from the pack, else Audio/<rel>.ogg / .wav in the game folder */
static int load(const char* key, const char* rel, au_dec* d)
{
    memset(d, 0, sizeof(*d));
    pack_entry* e = pack_file ? pack_find(key) : NULL;
    if (e) {
        d->data = (uint8_t*)malloc(e->length);
        if (!d->data) {
            au_log("audio: %s: no memory for %lu bytes", key, (unsigned long)e->length);
            return 0;
        }
        d->size = e->length;
        if (fseek(pack_file, (long)e->offset, SEEK_SET) != 0 || fread(d->data, 1, e->length, pack_file) != e->length) {
            dec_close(d);
            note_missing(key, "could not be read from the pack");
            return 0;
        }
    } else {
        static const char* const exts[] = { ".ogg", ".wav", "" };
        char path[512];
        for (int i = 0; i < 3 && !d->data; ++i) {
            snprintf(path, sizeof(path), "%sAudio/%s%s", game_dir, rel, exts[i]);
            d->data = read_whole(path, &d->size);
        }
        if (!d->data) {
            note_missing(key, pack_file ? "is not in the audio pack or Audio/" : "not found (no audio pack: tools/pack_audio.py)");
            return 0;
        }
    }
    if (!dec_open(d)) {
        dec_close(d);
        note_missing(key, "is not Ogg Vorbis or 16-bit PCM WAV");
        return 0;
    }
    return 1;
}

/* ---- streams ------------------------------------------------------------------------- */

int au_stream_read(au_stream* s, int16_t* out, int frames)
{
    int got = 0, ch = s->dec.channels, rewound = 0;
    while (got < frames) {
        int n = dec_read(&s->dec, out + (size_t)got * ch, frames - got);
        if (n > 0) {
            got += n;
            rewound = 0;
            continue;
        }
        if (s->loop && !rewound) {    /* twice in a row: an empty file */
            dec_rewind(&s->dec);
            rewound = 1;
            continue;
        }
        s->eof = 1;
        break;
    }
    s->frames += (uint64_t)got;
    return got;
}

static void stream_stop(int i)
{
    au_stream* s = &au_streams[i];
    if (s->open) aub_stream_stop(i);
    dec_close(&s->dec);
    memset(s, 0, sizeof(*s));
}

/* the BGM back after an ME */
static void bgm_unduck(void)
{
    au_stream* b = &au_streams[AU_BGM];
    if (!b->playing) return;
    if (b->paused) {
        aub_stream_pause(AU_BGM, 0);
        b->paused = 0;
    }
    b->duck_dir = +1;
}

static void stream_play(int i, const char* name, int vol, int pitch)
{
    char key[AU_NAME], rel[AU_NAME];
    make_key(name, key, rel);
    au_stream* s = &au_streams[i];
    float v = (vol < 0 ? 0 : vol > 100 ? 100 : vol) / 100.0f;
    if (pitch < 10) pitch = 10;
    if (s->playing && !strcmp(s->name, key) && s->pitch == pitch) {
        s->vol = v;            /* the same sound: only its volume (and no fade any more) */
        s->fade = 1;
        s->fade_ms = s->fade_left = 0;
        return;
    }
    stream_stop(i);
    if (!key[0]) return;
    if (!load(key, rel, &s->dec)) return;
    snprintf(s->name, sizeof(s->name), "%s", key);
    s->open = s->playing = 1;
    s->loop = i != AU_ME;
    s->vol = v;
    s->pitch = pitch;
    s->fade = s->duck = 1;
    aub_stream_start(i);
    if (i == AU_BGM && au_streams[AU_ME].playing) {    /* waits for the ME */
        s->duck = 0;
        s->paused = 1;
        aub_stream_pause(i, 1);
    }
    if (i == AU_ME && au_streams[AU_BGM].playing && !au_streams[AU_BGM].paused)
        au_streams[AU_BGM].duck_dir = -1;
}

/* ---- sound effects ------------------------------------------------------------------ */

static uint32_t use_clock;

static int voice_uses(int slot)
{
    for (int v = 0; v < AU_VOICES; ++v)
        if (au_voices[v].se == slot && aub_voice_busy(v)) return 1;
    return 0;
}

static void cache_drop(int slot)
{
    for (int v = 0; v < AU_VOICES; ++v)
        if (au_voices[v].se == slot) au_voices[v].se = -1;
    aub_pcm_free(au_cache[slot].pcm);
    memset(&au_cache[slot], 0, sizeof(au_se));
}

/* PCM memory for an SE, giving up the least recently used ones not playing */
static int16_t* pcm_alloc(size_t bytes)
{
    for (;;) {
        int16_t* p = (int16_t*)aub_pcm_alloc(bytes);
        if (p) return p;
        int lru = -1;
        for (int i = 0; i < au_cache_n; ++i)
            if (au_cache[i].pcm && !voice_uses(i) && (lru < 0 || au_cache[i].last_used < au_cache[lru].last_used)) lru = i;
        if (lru < 0) return NULL;
        cache_drop(lru);
    }
}

static int se_get(const char* name)
{
    char key[AU_NAME], rel[AU_NAME];
    make_key(name, key, rel);
    if (!key[0]) return -1;
    int free_slot = -1;
    for (int i = 0; i < au_cache_n; ++i) {
        if (au_cache[i].pcm && !strcmp(au_cache[i].name, key)) {
            au_cache[i].last_used = ++use_clock;
            au_stat_se_hits++;
            return i;
        }
        if (!au_cache[i].pcm && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) {
        if (au_cache_n == AU_CACHE_CAP) return -1;
        free_slot = au_cache_n++;
    }
    au_dec d;
    if (!load(key, rel, &d)) return -1;
    size_t total = d.vorbis ? stb_vorbis_stream_length_in_samples(d.vorbis) : d.wav_frames;
    size_t bytes = total * (size_t)d.channels * 2;
    int16_t* pcm = total ? pcm_alloc(bytes) : NULL;
    if (!pcm) {
        if (total) au_log("audio: %s: no room for %lu KB of sound", key, (unsigned long)(bytes / 1024));
        dec_close(&d);
        return -1;
    }
    size_t got = 0;
    while (got < total) {
        int n = dec_read(&d, pcm + got * (size_t)d.channels, (int)(total - got > 65536 ? 65536 : total - got));
        if (n <= 0) break;
        got += (size_t)n;
    }
    au_se* e = &au_cache[free_slot];
    snprintf(e->name, sizeof(e->name), "%s", key);
    e->pcm = pcm;
    e->frames = got;
    e->bytes = bytes;
    e->channels = d.channels;
    e->rate = d.rate;
    e->last_used = ++use_clock;
    dec_close(&d);
    au_stat_se_loads++;
    return free_slot;
}

static uint32_t voice_clock;

static void se_play(const char* name, int vol, int pitch)
{
    int slot = se_get(name);
    if (slot < 0 || au_cache[slot].frames == 0) return;
    int v = -1;
    for (int i = 0; i < AU_VOICES && v < 0; ++i)
        if (au_voices[i].se < 0 || !aub_voice_busy(i)) v = i;
    if (v < 0) {    /* all busy: the oldest gives way */
        v = 0;
        for (int i = 1; i < AU_VOICES; ++i)
            if (au_voices[i].started < au_voices[v].started) v = i;
    }
    aub_voice_stop(v);
    au_voice* vc = &au_voices[v];
    vc->se = slot;
    vc->vol = (vol < 0 ? 0 : vol > 100 ? 100 : vol) / 100.0f;
    vc->pitch = pitch < 10 ? 10 : pitch;
    vc->started = ++voice_clock;
    vc->pos = 0;
    aub_voice_play(v);
}

static void se_stop_all(void)
{
    for (int v = 0; v < AU_VOICES; ++v) {
        aub_voice_stop(v);
        au_voices[v].se = -1;
    }
}

/* ---- commands: queued by the game, run by au_service ----------------------------------- */

enum { C_PLAY, C_STOP, C_FADE, C_SE, C_SE_STOP };
typedef struct {
    int op, stream, vol, pitch, ms;
    char name[AU_NAME];
} au_cmd;

#define QUEUE 64
static au_cmd queue[QUEUE];
static int q_head, q_len;

static void push(int op, int stream, const char* name, int vol, int pitch, int ms)
{
    if (!aub_running()) return;
    aub_lock();
    if (q_len == QUEUE) {    /* the thread is far behind: the oldest goes */
        q_head = (q_head + 1) % QUEUE;
        q_len--;
    }
    au_cmd* c = &queue[(q_head + q_len++) % QUEUE];
    c->op = op;
    c->stream = stream;
    c->vol = vol;
    c->pitch = pitch;
    c->ms = ms;
    snprintf(c->name, sizeof(c->name), "%s", name ? name : "");
    aub_unlock();
    aub_wake();
}

static void run(const au_cmd* c)
{
    au_stream* s = &au_streams[c->stream];
    switch (c->op) {
    case C_PLAY:
        stream_play(c->stream, c->name, c->vol, c->pitch);
        break;
    case C_STOP:
        stream_stop(c->stream);
        if (c->stream == AU_ME) bgm_unduck();
        break;
    case C_FADE:
        if (!s->playing) break;
        if (c->ms <= 0) {
            stream_stop(c->stream);
            if (c->stream == AU_ME) bgm_unduck();
        } else {
            s->fade_ms = s->fade_left = c->ms;
        }
        break;
    case C_SE:
        se_play(c->name, c->vol, c->pitch);
        break;
    case C_SE_STOP:
        se_stop_all();
        break;
    }
}

void au_service(int elapsed_ms)
{
    au_cmd todo[QUEUE];
    int n = 0;
    aub_lock();     /* only to take the commands: loading and decoding run unlocked */
    while (q_len > 0) {
        todo[n++] = queue[q_head];
        q_head = (q_head + 1) % QUEUE;
        q_len--;
    }
    aub_unlock();
    for (int i = 0; i < n; ++i) run(&todo[i]);

    for (int i = 0; i < AU_STREAMS; ++i)
        if (au_streams[i].playing && !au_streams[i].paused) aub_stream_pump(i, elapsed_ms);

    au_stream* me = &au_streams[AU_ME];
    if (me->playing && aub_stream_drained(AU_ME)) {
        stream_stop(AU_ME);
        bgm_unduck();
    }
    for (int i = 0; i < AU_STREAMS; ++i) {
        au_stream* s = &au_streams[i];
        if (!s->playing) continue;
        if (s->fade_left > 0) {
            s->fade_left -= elapsed_ms;
            if (s->fade_left <= 0) {
                stream_stop(i);
                if (i == AU_ME) bgm_unduck();
                continue;
            }
            s->fade = (float)s->fade_left / (float)s->fade_ms;
        }
        if (i != AU_ME && s->eof && aub_stream_drained(i)) {    /* cannot happen when looping, but */
            stream_stop(i);
            continue;
        }
    }
    au_stream* b = &au_streams[AU_BGM];
    if (b->playing && b->duck_dir < 0) {    /* under an ME: quiet within 0.1 s, then paused */
        b->duck -= elapsed_ms / 100.0f;
        if (b->duck <= 0) {
            b->duck = 0;
            b->duck_dir = 0;
            b->paused = 1;
            aub_stream_pause(AU_BGM, 1);
        }
    } else if (b->playing && b->duck_dir > 0) {    /* back over a second */
        b->duck += elapsed_ms / 1000.0f;
        if (b->duck >= 1) {
            b->duck = 1;
            b->duck_dir = 0;
        }
    }
    for (int i = 0; i < AU_STREAMS; ++i) {
        au_stream* s = &au_streams[i];
        if (s->playing) aub_stream_mix(i, s->vol * s->fade * s->duck, s->dec.rate * s->pitch / 100.0f);
    }
    aub_tick(elapsed_ms);
    for (int v = 0; v < AU_VOICES; ++v)
        if (au_voices[v].se >= 0 && !aub_voice_busy(v)) au_voices[v].se = -1;
}

/* ---- Ruby ------------------------------------------------------------------------------ */

static int to_int(mrb_state* mrb, mrb_value v, int dflt)
{
    if (mrb_nil_p(v)) return dflt;
    if (mrb_integer_p(v)) return (int)mrb_integer(v);
    if (mrb_float_p(v)) return (int)mrb_float(v);
    return (int)mrb_as_int(mrb, v);
}

/* name, volume (RGSS's default for the kind), pitch 100 */
static mrb_value play_args(mrb_state* mrb, int op, int stream, int dflt_vol)
{
    mrb_value name, vol = mrb_nil_value(), pitch = mrb_nil_value();
    mrb_get_args(mrb, "S|oo", &name, &vol, &pitch);
    push(op, stream, mrb_str_to_cstr(mrb, name), to_int(mrb, vol, dflt_vol), to_int(mrb, pitch, 100), 0);
    return mrb_nil_value();
}

static mrb_value a_bgm_play(mrb_state* mrb, mrb_value self) { return play_args(mrb, C_PLAY, AU_BGM, 100); }
static mrb_value a_bgs_play(mrb_state* mrb, mrb_value self) { return play_args(mrb, C_PLAY, AU_BGS, 80); }
static mrb_value a_me_play(mrb_state* mrb, mrb_value self) { return play_args(mrb, C_PLAY, AU_ME, 100); }
static mrb_value a_se_play(mrb_state* mrb, mrb_value self) { return play_args(mrb, C_SE, 0, 80); }

static mrb_value stop_of(int stream) { push(C_STOP, stream, NULL, 0, 0, 0); return mrb_nil_value(); }
static mrb_value a_bgm_stop(mrb_state* mrb, mrb_value self) { return stop_of(AU_BGM); }
static mrb_value a_bgs_stop(mrb_state* mrb, mrb_value self) { return stop_of(AU_BGS); }
static mrb_value a_me_stop(mrb_state* mrb, mrb_value self) { return stop_of(AU_ME); }
static mrb_value a_se_stop(mrb_state* mrb, mrb_value self) { push(C_SE_STOP, 0, NULL, 0, 0, 0); return mrb_nil_value(); }

static mrb_value fade_of(mrb_state* mrb, int stream)
{
    mrb_value ms;
    mrb_get_args(mrb, "o", &ms);
    push(C_FADE, stream, NULL, 0, 0, to_int(mrb, ms, 0));
    return mrb_nil_value();
}
static mrb_value a_bgm_fade(mrb_state* mrb, mrb_value self) { return fade_of(mrb, AU_BGM); }
static mrb_value a_bgs_fade(mrb_state* mrb, mrb_value self) { return fade_of(mrb, AU_BGS); }
static mrb_value a_me_fade(mrb_state* mrb, mrb_value self) { return fade_of(mrb, AU_ME); }

/* Audio.ttm_open(pack path or nil, game folder): where sounds come from;
 * returns the number of sounds in the pack (0: none, loose files only) */
static mrb_value a_open(mrb_state* mrb, mrb_value self)
{
    mrb_value path, dir = mrb_nil_value();
    mrb_get_args(mrb, "o|o", &path, &dir);
    aub_lock();
    snprintf(game_dir, sizeof(game_dir), "%s", mrb_string_p(dir) ? mrb_str_to_cstr(mrb, dir) : "");
    int n = pack_open(mrb_string_p(path) ? mrb_str_to_cstr(mrb, path) : NULL);
    aub_unlock();
    return mrb_int_value(mrb, n);
}

/* Audio.ttm_advance(ms): the PC's clock (the 3DS's thread keeps its own) */
static mrb_value a_advance(mrb_state* mrb, mrb_value self)
{
    mrb_int ms;
    mrb_get_args(mrb, "i", &ms);
#ifndef __3DS__
    au_service((int)ms);
#endif
    return mrb_nil_value();
}

static void hset(mrb_state* mrb, mrb_value h, const char* k, mrb_value v)
{
    mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_cstr(mrb, k)), v);
}

/* Audio.ttm_state: [{bgm}, {bgs}, {me}, [[se name, volume, pitch, pos] per busy voice]] */
static mrb_value a_state(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new(mrb);
    for (int i = 0; i < AU_STREAMS; ++i) {
        au_stream* s = &au_streams[i];
        mrb_value h = mrb_hash_new(mrb);
        hset(mrb, h, "name", mrb_str_new_cstr(mrb, s->name));
        hset(mrb, h, "playing", mrb_bool_value(s->playing));
        hset(mrb, h, "paused", mrb_bool_value(s->paused));
        hset(mrb, h, "volume", mrb_float_value(mrb, s->vol * s->fade * s->duck));
        hset(mrb, h, "pitch", mrb_int_value(mrb, s->pitch));
        hset(mrb, h, "frames", mrb_int_value(mrb, (mrb_int)s->frames));
        hset(mrb, h, "rate", mrb_int_value(mrb, s->dec.rate));
        mrb_ary_push(mrb, a, h);
    }
    mrb_value voices = mrb_ary_new(mrb);
    for (int v = 0; v < AU_VOICES; ++v) {
        au_voice* vc = &au_voices[v];
        if (vc->se < 0 || !aub_voice_busy(v)) continue;
        mrb_value e = mrb_ary_new(mrb);
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, au_cache[vc->se].name));
        mrb_ary_push(mrb, e, mrb_float_value(mrb, vc->vol));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, vc->pitch));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, (mrb_int)vc->started));
        mrb_ary_push(mrb, voices, e);
    }
    mrb_ary_push(mrb, a, voices);
    return a;
}

/* Audio.ttm_pack: [[key, channels, rate, frames], ...] */
static mrb_value a_pack(mrb_state* mrb, mrb_value self)
{
    mrb_value a = mrb_ary_new(mrb);
    for (int i = 0; i < pack_n; ++i) {
        mrb_value e = mrb_ary_new(mrb);
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, pack[i].key));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, pack[i].channels));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, pack[i].rate));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, pack[i].frames));
        mrb_ary_push(mrb, a, e);
    }
    return a;
}

/* Audio.ttm_decode(name): decode a whole sound -> [channels, rate, frames], nil if not found */
static mrb_value a_decode(mrb_state* mrb, mrb_value self)
{
    char* name;
    mrb_get_args(mrb, "z", &name);
    char key[AU_NAME], rel[AU_NAME];
    make_key(name, key, rel);
    au_dec d;
    if (!load(key, rel, &d)) return mrb_nil_value();
    static int16_t buf[8192];
    long frames = 0;
    int n;
    while ((n = dec_read(&d, buf, 4096)) > 0) frames += n;
    mrb_value a = mrb_ary_new(mrb);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, d.channels));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, d.rate));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, frames));
    dec_close(&d);
    return a;
}

/* Audio.ttm_stats: [SEs decoded, SE plays from the cache, missing-sound requests,
 * KB of SE sound kept, KB of PCM memory free] */
static mrb_value a_stats(mrb_state* mrb, mrb_value self)
{
    long kb = 0;
    for (int i = 0; i < au_cache_n; ++i) kb += (long)(au_cache[i].bytes / 1024);
    mrb_value a = mrb_ary_new(mrb);
    mrb_ary_push(mrb, a, mrb_int_value(mrb, au_stat_se_loads));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, au_stat_se_hits));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, au_stat_missing));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, kb));
    mrb_ary_push(mrb, a, mrb_int_value(mrb, aub_pcm_free_kb()));
    return a;
}

void mrb_mruby_ttm_audio_gem_init(mrb_state* mrb)
{
    struct RClass* a = mrb_define_module(mrb, "Audio");
    if (!au_cache) {
        au_cache = (au_se*)calloc(AU_CACHE_CAP, sizeof(au_se));
        for (int v = 0; v < AU_VOICES; ++v) au_voices[v].se = -1;
    }
    mrb_define_module_function(mrb, a, "bgm_play", a_bgm_play, MRB_ARGS_ARG(1, 2));
    mrb_define_module_function(mrb, a, "bgm_stop", a_bgm_stop, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, a, "bgm_fade", a_bgm_fade, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, a, "bgs_play", a_bgs_play, MRB_ARGS_ARG(1, 2));
    mrb_define_module_function(mrb, a, "bgs_stop", a_bgs_stop, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, a, "bgs_fade", a_bgs_fade, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, a, "me_play", a_me_play, MRB_ARGS_ARG(1, 2));
    mrb_define_module_function(mrb, a, "me_stop", a_me_stop, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, a, "me_fade", a_me_fade, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, a, "se_play", a_se_play, MRB_ARGS_ARG(1, 2));
    mrb_define_module_function(mrb, a, "se_stop", a_se_stop, MRB_ARGS_NONE());
    /* engine */
    mrb_define_module_function(mrb, a, "ttm_open", a_open, MRB_ARGS_ARG(1, 1));
    mrb_define_module_function(mrb, a, "ttm_advance", a_advance, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, a, "ttm_state", a_state, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, a, "ttm_pack", a_pack, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, a, "ttm_decode", a_decode, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, a, "ttm_stats", a_stats, MRB_ARGS_NONE());
}

/* the pack is closed here, in mrb_close (the 3DS app stops the audio thread
 * first). Left open, it crashed the console on quitting: main.c's romfsExit
 * removes romfs, and newlib's exit then fclose'd romfs:/audio.ttma through a
 * device that was no longer there (console run 12, Exit Game > Shutdown). */
void mrb_mruby_ttm_audio_gem_final(mrb_state* mrb)
{
    (void)mrb;
    pack_open(NULL);
}

/* the voice's sound (backends) */
const au_se* au_voice_se(int v)
{
    int s = au_voices[v].se;
    return s >= 0 && au_cache ? &au_cache[s] : NULL;
}

const char* au_stream_name(int i) { return STREAM_NAMES[i]; }
