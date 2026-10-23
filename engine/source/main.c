/*
 * main.c -- the To the Moon 3DS engine app.
 *
 * Runs To the Moon from the player's own files on the SD card, with the engine's
 * C classes (mruby-ttm-*) and the game's own scripts:
 *   sdmc:/3ds/ttm3ds/To the Moon.rgssad        (the game's archive)
 *   sdmc:/3ds/ttm3ds/Fonts/OpenSans.ttf        (the game's font)
 * If sdmc:/3ds/ttm3ds/ has no archive, sdmc:/3ds/ttm3ds_ruby/ is tried (where
 * the M2 test kept it). The app itself contains none of the game's files.
 *
 * The same folder is where the game writes: its saves (save/save1..4.rxdata), the
 * settings files it keeps beside them (music.txt, window.txt) and the log below.
 * rgss/compat.rb's TTMFiles maps every path the game's File/Dir touches under it.
 *
 * Top screen: the game, its 640x480 frame drawn at half size (backend_c3d.c),
 * with simple stereoscopic depth from the 3D slider.
 * Bottom screen: while the game runs, the game's own menu at half size with
 * its heart-monitor strip, for the stylus (romfs/rgss/ttm_patches.rb,
 * TTMBottomMenu; TTM.touch below); before it starts and after an error, a
 * text console -- once the game is found the boot lines go to the log only.
 * The log always goes to sdmc:/3ds/ttm3ds/log.txt, with a "perf:" line every
 * five seconds.
 *
 * Buttons -> RGSS keys: A = C (confirm), B = B (cancel / menu), Y = A, X = X,
 * R; D-pad or Circle Pad = directions; START = B. L is the timeline (the
 * engine's own key 99, Input::TTM_TIMELINE), never RGSS's L: the game's
 * common event 85 fast-forwards to 160 fps while RGSS's L is held.
 * START + SELECT quits.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include <3ds.h>
#include <citro2d.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/variable.h>

/* the parser and code generator recurse on the C stack */
u32 __stacksize__ = 1024 * 1024;
/* textures live in linear memory: a 640x480 picture is a 2 MB texture */
u32 __ctru_linear_heap_size = 48 * 1024 * 1024;

void ttm_gfx3ds_init(void);   /* gems/mruby-ttm-gfx/src/backend_c3d.c */
void gfx_frame_reset(void);   /* same file: pace from now, no catch-up */
int ttm_audio3ds_init(void);  /* gems/mruby-ttm-audio/src/backend_ndsp.c */
void ttm_audio3ds_exit(void);
extern void (*ttm_audio_log_fn)(const char* line);    /* its log lines (audio thread) */
extern int ttm_top_stretch;   /* same file: full-screen 5:3 or 4:3 top screen */
extern void (*ttm_gfx_log_fn)(const char* line);      /* its once-a-second figures */
/* same file: the bottom screen as the game's second screen, or the console */
void ttm_bottom_start(void);
void ttm_bottom_stop(void);
static int console_on;        /* the bottom screen is the text console (not the game's menu) */
static int console_ready;     /* consoleInit has run (run 19: the launcher draws instead) */

/* The text console is created on demand: the launcher is citro2d (run 19), and
 * the two cannot share the bottom screen. The error and missing-files screens
 * call this first; ttm_bottom_stop() also makes one when the game ends. */
static void ensure_console(void)
{
    if (!console_ready) {
        consoleInit(GFX_BOTTOM, NULL);
        console_ready = 1;
    }
    console_on = 1;
}

/* devkitARM's newlib declares C11 timespec_get() but does not implement it;
 * mruby-time calls it for Time.now. libctru does implement gettimeofday(). */
int timespec_get(struct timespec* ts, int base)
{
    struct timeval tv;
    if (base != TIME_UTC || gettimeofday(&tv, NULL) != 0) return 0;
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = (long)tv.tv_usec * 1000;
    return base;
}

#define APP_DIR "sdmc:/3ds/ttm3ds/"
static FILE* log_file;
static LightLock log_lock;    /* the audio thread writes to log.txt too */

static mrb_value ttm_log(mrb_state* mrb, mrb_value self)
{
    const char* s;
    mrb_int n;
    mrb_get_args(mrb, "s", &s, &n);
    if (console_on) printf("%.*s\n", (int)n, s);
    if (log_file) {
        LightLock_Lock(&log_lock);
        fprintf(log_file, "%.*s\n", (int)n, s);
        fflush(log_file);
        LightLock_Unlock(&log_lock);
    }
    return mrb_nil_value();
}

/* the audio thread's lines, and the graphics' perf: lines: log.txt only (the
 * console is the game thread's) */
static void audio_log(const char* line)
{
    if (!log_file) return;
    LightLock_Lock(&log_lock);
    fprintf(log_file, "%s\n", line);
    fflush(log_file);
    LightLock_Unlock(&log_lock);
}

/* the RGSS keys held now; called once a frame by Input.update */
static mrb_value ttm_keys(mrb_state* mrb, mrb_value self)
{
    hidScanInput();
    u32 k = hidKeysHeld();
    if ((k & KEY_START) && (k & KEY_SELECT))
        mrb_raise(mrb, mrb_class_get(mrb, "TTMQuit"), "START + SELECT");
    /* SELECT alone switches the top screen between full screen and 4:3; the
     * game never sees it (RGSS has no SELECT) */
    if ((hidKeysDown() & KEY_SELECT) && !(k & KEY_START))
        ttm_top_stretch = !ttm_top_stretch;
    static const struct { u32 key; int rgss; } map[] = {
        { KEY_A, 13 }, { KEY_B, 12 }, { KEY_Y, 11 }, { KEY_X, 14 }, { KEY_L, 99 }, { KEY_R, 18 },
        { KEY_START, 12 }, { KEY_UP, 8 }, { KEY_DOWN, 2 }, { KEY_LEFT, 4 }, { KEY_RIGHT, 6 },
    };
    mrb_value a = mrb_ary_new(mrb);
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (k & map[i].key) mrb_ary_push(mrb, a, mrb_fixnum_value(map[i].rgss));
    return a;
}

/* the stylus this frame, as read by the TTM.keys call before it: [x, y] on
 * the bottom screen (320x240), or nil. Input.update maps it to game space. */
static mrb_value ttm_touch(mrb_state* mrb, mrb_value self)
{
    if (console_on || !(hidKeysHeld() & KEY_TOUCH)) return mrb_nil_value();
    touchPosition tp;
    hidTouchRead(&tp);
    mrb_value v[2] = { mrb_fixnum_value(tp.px), mrb_fixnum_value(tp.py) };
    return mrb_ary_new_from_values(mrb, 2, v);
}

/* back from the HOME menu or sleep: libctru has put the CPU speedup back
 * (osSetSpeedupEnable), and the time away must not be caught up by frame skip */
static int resumes;
static void on_apt(APT_HookType hook, void* param)
{
    (void)param;
    if (hook != APTHOOK_ONRESTORE && hook != APTHOOK_ONWAKEUP) return;
    gfx_frame_reset();
    resumes++;
    if (log_file) {
        LightLock_Lock(&log_lock);
        fprintf(log_file, "apt: resumed (%s, %d so far)\n", hook == APTHOOK_ONRESTORE ? "HOME" : "sleep", resumes);
        fflush(log_file);
        LightLock_Unlock(&log_lock);
    }
}

static int file_exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* The font a game with no Fonts/ folder of its own draws with. The CIA carries
 * the main game's fonts at romfs:/game/Fonts/, so that copy comes first: run 19
 * found the minisodes silent on a console with no files on the SD card, because
 * this path was the card's Fonts/ only (boot.rb then had nothing to load and
 * every draw_text drew no glyphs -- the windows and name tabs were there). */
static const char* shared_font(void)
{
    static const char* candidates[] = {
        "romfs:/game/Fonts/OpenSans.ttf",
        APP_DIR "Fonts/OpenSans.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (file_exists(candidates[i])) return candidates[i];
    return APP_DIR "Fonts/OpenSans.ttf";      /* boot.rb logs the warning */
}

/* ---- which game ---------------------------------------------------------------
 * The app runs any RGSS1 game whose archive is on the card: the main game's
 * "To the Moon.rgssad", and the two Sigmund minisodes, which are whole RGSS1
 * projects of their own in their own folders. The list is APP_DIR plus one
 * folder below it, each *.rgssad found; a game's title comes from the *.ini
 * beside its archive ("[Game]"'s "Title="), else from the archive's name.
 *
 * One game on the card boots straight in, exactly as before. Two or more get
 * the launcher on the bottom screen (tap a title, or D-pad + A), and the last
 * choice is remembered in APP_DIR "last-game.txt". A minisode writes in
 * APP_DIR "games/<its folder>/", so it can never touch the main game's saves.
 */
#define MAX_GAMES 8

typedef struct {
    char dir[192];      /* the archive's folder, with a trailing '/' */
    char archive[96];   /* the archive's file name */
    char title[64];     /* what the launcher shows */
    char id[64];        /* the folder's name: games/<id>/, and last-game.txt */
    int main_game;      /* APP_DIR itself: the main game keeps its saves there */
} game_t;

static game_t games[MAX_GAMES];
static int n_games;

static int has_suffix(const char* s, const char* suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    if (b > a) return 0;
    for (size_t i = 0; i < b; ++i) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != suffix[i]) return 0;
    }
    return 1;
}

/* "[Game]"'s "Title=" from an ini beside the archive; 0 when there is none */
static int ini_title(const char* path, char* out, size_t n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char line[256];
    int in_game = 0, got = 0;
    while (!got && fgets(line, sizeof(line), f)) {
        if (line[0] == '[') { in_game = strncmp(line, "[Game]", 6) == 0; continue; }
        if (!in_game || strncmp(line, "Title=", 6) != 0) continue;
        char* v = line + 6;
        size_t len = strlen(v);
        while (len && (v[len - 1] == '\n' || v[len - 1] == '\r' || v[len - 1] == ' ')) v[--len] = 0;
        if (len) { snprintf(out, n, "%s", v); got = 1; }
    }
    fclose(f);
    return got;
}

static int game_id_seen(const char* id)
{
    for (int i = 0; i < n_games; ++i)
        if (strcmp(games[i].id, id) == 0) return 1;
    return 0;
}

static void add_game(const char* dir, const char* archive, const char* id, int main_game)
{
    if (n_games >= MAX_GAMES) return;
    game_t* g = &games[n_games];
    snprintf(g->dir, sizeof(g->dir), "%s", dir);
    snprintf(g->archive, sizeof(g->archive), "%s", archive);
    snprintf(g->id, sizeof(g->id), "%s", id);
    g->main_game = main_game;
    /* the title: <archive>.ini, else Game.ini, else the archive's name */
    char ini[256];
    snprintf(ini, sizeof(ini), "%s%s", dir, archive);
    size_t len = strlen(ini);
    while (len > 0 && ini[len - 1] != '.') --len;
    if (len > 0) ini[len - 1] = 0;                       /* cut ".rgssad" */
    strncat(ini, ".ini", sizeof(ini) - strlen(ini) - 1);
    char fallback[256];
    snprintf(fallback, sizeof(fallback), "%sGame.ini", dir);
    if (!ini_title(ini, g->title, sizeof(g->title)) &&
        !ini_title(fallback, g->title, sizeof(g->title))) {
        snprintf(g->title, sizeof(g->title), "%s", archive);
        char* dot = strrchr(g->title, '.');
        if (dot) *dot = 0;                              /* never show ".rgssad" */
    }
    ++n_games;
}

static void scan_games(void)
{
    n_games = 0;
    DIR* d = opendir(APP_DIR);
    if (d) {                                             /* the app's own folder */
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (!has_suffix(e->d_name, ".rgssad")) continue;
            if (strcmp(e->d_name, "To the Moon.rgssad") == 0) {
                add_game(APP_DIR, e->d_name, "ttm", 1);
            } else {                                     /* another game beside it */
                char base[256];
                snprintf(base, sizeof(base), "%s", e->d_name);
                char* dot = strrchr(base, '.');
                if (dot) *dot = 0;
                add_game(APP_DIR, e->d_name, base, 0);
            }
        }
        closedir(d);
    }
    d = opendir(APP_DIR);
    if (d) {                                             /* one folder down: a minisode on the card */
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char sub[320];
            snprintf(sub, sizeof(sub), "%s%s/", APP_DIR, e->d_name);
            DIR* s = opendir(sub);
            if (!s) continue;                            /* a file, not a folder */
            struct dirent* f;
            while ((f = readdir(s)) != NULL) {
                if (!has_suffix(f->d_name, ".rgssad")) continue;
                add_game(sub, f->d_name, e->d_name, 0);
                break;                                   /* one game per folder */
            }
            closedir(s);
        }
        closedir(d);
    }
    /* the main game the app carries (romfs:/game/, build.ps1's pack_romfs.py):
     * the card's copy wins, and the saves stay in APP_DIR either way */
    if (!game_id_seen("ttm") && file_exists("romfs:/game/To the Moon.rgssad"))
        add_game("romfs:/game/", "To the Moon.rgssad", "ttm", 1);
    /* the games the app carries (build.ps1's romfs:/games/, made by
     * tools/pack_romfs.py): a folder of the same name on the card wins, so
     * an episode copied to the SD card replaces the built-in one */
    d = opendir("romfs:/games/");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            if (game_id_seen(e->d_name)) continue;
            char sub[320];
            snprintf(sub, sizeof(sub), "romfs:/games/%s/", e->d_name);
            DIR* s = opendir(sub);
            if (!s) continue;
            struct dirent* f;
            while ((f = readdir(s)) != NULL) {
                if (!has_suffix(f->d_name, ".rgssad")) continue;
                add_game(sub, f->d_name, e->d_name, 0);
                break;
            }
            closedir(s);
        }
        closedir(d);
    }
    for (int i = 1; i < n_games; ++i)                    /* the main game first */
        if (games[i].main_game) {
            game_t t = games[i];
            for (int j = i; j > 0; --j) games[j] = games[j - 1];
            games[0] = t;
            break;
        }
}

/* ---- the launcher's screen ------------------------------------------------------
 * citro2d, in the game's own navy and gold (run 19): the text console listed the
 * games in 8x8 white and read like a debug prompt. The top screen shows the
 * chosen game large, the bottom screen the list you tap or step through. The
 * console is no longer created at startup -- ensure_console() makes it for the
 * messages -- so nothing redraws over this frame.
 */
#define UI_ROW_Y    34.0f
#define UI_ROW_H    42.0f
#define UI_ROW_GAP  8.0f
#define UI_ROW_X    10.0f
#define UI_ROW_W    300.0f

static C3D_RenderTarget* ui_top;
static C3D_RenderTarget* ui_bottom;
static C2D_TextBuf ui_buf;

static void ui_open(void)
{
    if (ui_top) return;
    gfxSet3D(false);
    ui_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    ui_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui_buf = C2D_TextBufNew(4096);
    if (log_file) {
        fprintf(log_file, "ui: launcher targets up, %lu KB VRAM free\n",
                (unsigned long)(vramSpaceFree() / 1024));
        fflush(log_file);
    }
}

static void ui_close(void)
{
    if (ui_buf) { C2D_TextBufDelete(ui_buf); ui_buf = NULL; }
    if (ui_top) { C3D_RenderTargetDelete(ui_top); ui_top = NULL; }
    if (ui_bottom) { C3D_RenderTargetDelete(ui_bottom); ui_bottom = NULL; }
    if (log_file) {
        fprintf(log_file, "ui: launcher targets down, %lu KB VRAM free\n",
                (unsigned long)(vramSpaceFree() / 1024));
        fflush(log_file);
    }
}

/* a vertical gradient in a few flat strips (citro2d has no gradient primitive) */
static void ui_shade(C3D_RenderTarget* t, int w, int h, int r0, int g0, int b0,
                     int r1, int g1, int b1)
{
    C2D_TargetClear(t, C2D_Color32(r0, g0, b0, 255));
    C2D_SceneBegin(t);
    const int strips = 12;
    for (int i = 0; i < strips; ++i) {
        float k = (float)i / (strips - 1);
        u32 c = C2D_Color32(r0 + (int)((r1 - r0) * k), g0 + (int)((g1 - g0) * k),
                            b0 + (int)((b1 - b0) * k), 255);
        C2D_DrawRectSolid(0, (float)(h * i / strips), 0, (float)w,
                          (float)(h / strips + 1), c);
    }
}

static void ui_text(const char* s, float x, float y, float scale, u32 colour)
{
    C2D_Text t;
    C2D_TextParse(&t, ui_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, scale, scale, colour);
}

static float ui_width(const char* s, float scale)
{
    C2D_Text t;
    float w = 0, h = 0;
    C2D_TextParse(&t, ui_buf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static void ui_panel(float x, float y, float w, float h, float r, u32 colour)
{
    C2D_DrawRectSolid(x + r, y, 0, w - 2 * r, h, colour);
    C2D_DrawRectSolid(x, y + r, 0, r, h - 2 * r, colour);
    C2D_DrawRectSolid(x + w - r, y + r, 0, r, h - 2 * r, colour);
    C2D_DrawCircleSolid(x + r, y + r, 0, r, colour);
    C2D_DrawCircleSolid(x + w - r, y + r, 0, r, colour);
    C2D_DrawCircleSolid(x + r, y + h - r, 0, r, colour);
    C2D_DrawCircleSolid(x + w - r, y + h - r, 0, r, colour);
}

/* the row height: 42 px, squeezed only if the list is long */
static float launcher_row_h(void)
{
    float avail = 240 - UI_ROW_Y - 28;
    float h = UI_ROW_H;
    if (n_games > 0 && n_games * h + (n_games - 1) * UI_ROW_GAP > avail)
        h = (avail - (n_games - 1) * UI_ROW_GAP) / n_games;
    return h < 20 ? 20 : h;
}

/* both targets and the text buffer, or the launcher falls back to the console */
static int ui_ready(void)
{
    return ui_top && ui_bottom && ui_buf;
}

/* no VRAM for the graphical launcher: the plain list, exactly as before run 20 */
static void draw_launcher_console(int sel)
{
    ensure_console();
    consoleClear();
    printf("To the Moon 3DS -- choose a game\n\n");
    for (int i = 0; i < n_games; ++i)
        printf("%s %s\n", i == sel ? ">" : " ", games[i].title);
    printf("\n  A: play    START+SELECT: quit\n");
}

static void draw_launcher(int sel)
{
    if (!ui_top) return;
    const u32 gold = C2D_Color32(230, 194, 131, 255);
    const u32 card = C2D_Color32(27, 36, 51, 255);
    const u32 ink = C2D_Color32(223, 230, 242, 255);
    const u32 dim = C2D_Color32(127, 138, 158, 255);
    const u32 on_gold = C2D_Color32(16, 24, 32, 255);
    char line[96];

    C2D_TextBufClear(ui_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    /* ---- top screen: who we are, and what is chosen ---- */
    ui_shade(ui_top, 400, 240, 8, 13, 21, 24, 34, 50);
    C2D_DrawCircleSolid(338, 54, 0, 26, gold);                 /* the crescent */
    C2D_DrawCircleSolid(349, 45, 0, 23, C2D_Color32(18, 26, 40, 255));
    ui_text("TO THE MOON 3DS", 24, 20, 1.0f, gold);
    C2D_DrawRectSolid(24, 38, 0, 236, 1, C2D_Color32(230, 194, 131, 120));
    if (n_games > 0) {
        const char* title = games[sel].title;
        float scale = 2.0f;
        while (scale > 1.0f && ui_width(title, scale) > 352) scale -= 0.1f;
        ui_text(title, 24, 96, scale, ink);
        ui_text(games[sel].main_game ? "the main game" : "a Sigmund minisode",
                26, 140, 1.0f, dim);
        snprintf(line, sizeof(line), "%d game%s here", n_games, n_games == 1 ? "" : "s");
        ui_text(line, 24, 204, 0.8f, C2D_Color32(96, 106, 124, 255));
    }

    /* ---- bottom screen: the list ---- */
    ui_shade(ui_bottom, 320, 240, 12, 18, 28, 20, 28, 42);
    ui_text("CHOOSE A GAME", UI_ROW_X + 6, 10, 0.8f, dim);
    float row_h = launcher_row_h();
    for (int i = 0; i < n_games; ++i) {
        float y = UI_ROW_Y + i * (row_h + UI_ROW_GAP);
        int on = i == sel;
        ui_panel(UI_ROW_X, y, UI_ROW_W, row_h, 8, on ? gold : card);
        if (on) {
            float cy = y + row_h / 2;
            C2D_DrawTriangle(UI_ROW_X + 8, cy - 5, on_gold,
                             UI_ROW_X + 8, cy + 5, on_gold,
                             UI_ROW_X + 14, cy, on_gold, 0);
        }
        float scale = 1.15f;
        while (scale > 0.8f && ui_width(games[i].title, scale) > UI_ROW_W - 34)
            scale -= 0.05f;
        ui_text(games[i].title, UI_ROW_X + 20, y + (row_h - 8 * scale) / 2 - 1,
                scale, on ? on_gold : ink);
    }
    ui_text("A  select        START+SELECT  quit", UI_ROW_X + 6, 218, 0.8f, dim);

    C3D_FrameEnd(0);
}

/* the index of the chosen game, or -1 to quit */
static int choose_game(void)
{
    if (n_games == 0) return -1;
    if (n_games == 1) return 0;
    int sel = 0;
    FILE* f = fopen(APP_DIR "last-game.txt", "rb");      /* the last choice */
    if (f) {
        char line[64] = { 0 };
        if (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
            for (int i = 0; i < n_games; ++i)
                if (strcmp(games[i].id, line) == 0) { sel = i; break; }
        }
        fclose(f);
    }
    ui_open();
    int text_ui = !ui_ready();
    if (text_ui) draw_launcher_console(sel); else draw_launcher(sel);
    for (;;) {
        hidScanInput();
        u32 down = hidKeysDown();
        if ((hidKeysHeld() & KEY_START) && (hidKeysHeld() & KEY_SELECT)) {
            ui_close();
            return -1;
        }
        if (down & KEY_DOWN) {
            sel = (sel + 1) % n_games;
            if (text_ui) draw_launcher_console(sel); else draw_launcher(sel);
        }
        if (down & KEY_UP) {
            sel = (sel + n_games - 1) % n_games;
            if (text_ui) draw_launcher_console(sel); else draw_launcher(sel);
        }
        if (down & KEY_A) break;
        if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (text_ui) {
                int line = tp.py / 8 - 2;                    /* titles from line 2 */
                if (line >= 0 && line < n_games) { sel = line; break; }
            } else {
                float row_h = launcher_row_h();
                if (tp.py >= UI_ROW_Y) {
                    int row = (int)((tp.py - UI_ROW_Y) / (row_h + UI_ROW_GAP));
                    float in_row = tp.py - (UI_ROW_Y + row * (row_h + UI_ROW_GAP));
                    if (row >= 0 && row < n_games && in_row <= row_h) { sel = row; break; }
                }
            }
        }
        if (!aptMainLoop()) { ui_close(); return -1; }
        gspWaitForVBlank();
    }
    ui_close();
    f = fopen(APP_DIR "last-game.txt", "wb");
    if (f) { fprintf(f, "%s\n", games[sel].id); fclose(f); }
    if (log_file) fprintf(log_file, "launcher: %s (%s%s)\n", games[sel].title,
                           games[sel].dir, games[sel].archive);
    return sel;
}

static void wait_for_start(void)
{
    ensure_console();
    printf("\nSTART: exit\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

static void report_exception(mrb_state* mrb)
{
    ensure_console();
    mrb_value exc = mrb_obj_value(mrb->exc);
    mrb->exc = NULL;
    mrb_value msg = mrb_inspect(mrb, exc);
    printf("\nERROR %.*s\n", (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
    if (log_file) fprintf(log_file, "ERROR %.*s\n", (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
    mrb_value bt = mrb_funcall(mrb, exc, "backtrace", 0);
    if (mrb_array_p(bt)) {
        for (mrb_int i = 0; i < RARRAY_LEN(bt) && i < 8; ++i) {
            mrb_value line = mrb_ary_entry(bt, i);
            if (!mrb_string_p(line)) continue;
            printf("  %.*s\n", (int)RSTRING_LEN(line), RSTRING_PTR(line));
            if (log_file) fprintf(log_file, "  %.*s\n", (int)RSTRING_LEN(line), RSTRING_PTR(line));
        }
    }
    if (log_file) fflush(log_file);
    mrb->exc = NULL;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    /* The log first, before the graphics come up (run 19): a console hang that
     * showed nothing on either screen left no evidence at all, because the log
     * was only opened after gfxInitDefault. sdmc needs no graphics, so the file
     * and its dated first line exist from the first millisecond. */
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DIR, 0777);
    LightLock_Init(&log_lock);
    log_file = fopen(APP_DIR "log.txt", "w");
    if (log_file) {
        fprintf(log_file, "engine M7, run 19 build (the ending's Steam hook, the "
                          "minigames' controls, the animated banner; run 16: the launcher "
                          "and the two minisodes; run 15: languages; run 14: the heart "
                          "strip, transitions, simple 3D, CIA)\n");
        fflush(log_file);
    }

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    romfsInit();

    /* New 3DS: 804 MHz + L2. Through libctru's flag, not PTMSYSM directly:
     * the HOME menu resets the clock, and libctru's resume code reapplies only
     * what this flag says -- set by hand, every return from HOME left the
     * game at 268 MHz without L2 (console run 10) */
    osSetSpeedupEnable(true);
    static aptHookCookie apt_cookie;
    aptHook(&apt_cookie, on_apt, NULL);

    /* which game: the app's folder and one folder below it (run 16), with the
     * M2 test's folder kept as the last resort for an older card */
    scan_games();
    if (n_games == 0 && file_exists("sdmc:/3ds/ttm3ds_ruby/To the Moon.rgssad"))
        add_game("sdmc:/3ds/ttm3ds_ruby/", "To the Moon.rgssad", "ttm", 1);
    int chosen = choose_game();
    const game_t* game = chosen >= 0 ? &games[chosen] : NULL;
    const char* game_dir = game ? game->dir : NULL;
    char path[320];
    if (!game && n_games > 0) goto cleanup;      /* START + SELECT in the launcher */

    /* With the game found the bottom screen stops being a console here: the
     * boot lines go to log.txt only, and the missing-files and error messages
     * keep the console. (Until run 13 they stayed on the bottom screen from
     * launch until the first frame.) The console is made on demand since run 19,
     * because the launcher draws with citro2d. */
    if (!game_dir) ensure_console();
    if (!game_dir) printf("To the Moon 3DS -- engine M7, run 19 build\n");
    if (!game_dir) printf("heap %lu KB, linear %lu KB\n", (unsigned long)(envGetHeapSize() / 1024),
                          (unsigned long)(envGetLinearHeapSize() / 1024));
    if (log_file) fprintf(log_file, "heap %lu KB, linear %lu KB\n", (unsigned long)(envGetHeapSize() / 1024),
                          (unsigned long)(envGetLinearHeapSize() / 1024));
    /* the console's clock: save slots are dated with it (File#mtime) */
    {
        time_t now = time(NULL);
        char when[32];
        strftime(when, sizeof(when), "%Y/%m/%d %H:%M", gmtime(&now));
        if (!game_dir) printf("clock %s\n", when);
        if (log_file) fprintf(log_file, "clock %s\n", when);
    }

    if (!game_dir) {
        ensure_console();
        printf("\nNo game found. Copy these from your copy of the\n"
               "game (its install folder) to the SD card:\n\n"
               "  To the Moon.rgssad\n"
               "  Fonts/OpenSans.ttf\n\n"
               "and, to play the game in another language,\n"
               "  lang.dat                            (the translations)\n"
               "  Fonts/NanumBarunGothic.ttf          (Korean)\n"
               "  Fonts/WenQuanYi_Zen_Hei.ttf         (Chinese)\n\n"
               "The two minisodes are whole games of their own:\n"
               "  Sigmund Holiday Special 1/          (its .rgssad, Audio/)\n"
               "  Sigmund Holiday Special 2/\n\n"
               "into  %s\n", APP_DIR);
        wait_for_start();
    } else {
        /* the hooks first: ttm_gfx3ds_init's VRAM lines belong in log.txt */
        ttm_audio_log_fn = audio_log;
        ttm_gfx_log_fn = audio_log;
        ttm_gfx3ds_init();
        /* sound: before the textures can fill the linear heap (it reserves its share) */
        ttm_audio3ds_init();
        mrb_state* mrb = mrb_open();
        struct RClass* ttm = mrb_module_get(mrb, "TTM");
        mrb_define_module_function(mrb, ttm, "log", ttm_log, MRB_ARGS_REQ(1));
        mrb_define_module_function(mrb, ttm, "keys", ttm_keys, MRB_ARGS_NONE());
        mrb_define_module_function(mrb, ttm, "touch", ttm_touch, MRB_ARGS_NONE());
        snprintf(path, sizeof(path), "%s%s", game->dir, game->archive);
        mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_game_dir"), mrb_str_new_cstr(mrb, game->dir));
        mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_archive"), mrb_str_new_cstr(mrb, path));
        mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_game_id"), mrb_str_new_cstr(mrb, game->id));
        /* where the game may write: its saves, its settings (rgss/compat.rb's
         * TTMFiles maps every path its File/Dir sees under this one folder).
         * The main game keeps APP_DIR, so its saves stay where they were; a
         * minisode gets games/<its folder>/, its own from top to bottom. */
        char data_dir[256];
        if (game->main_game)
            snprintf(data_dir, sizeof(data_dir), "%s", APP_DIR);
        else
            snprintf(data_dir, sizeof(data_dir), APP_DIR "games/%s/", game->id);
        mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_data_dir"), mrb_str_new_cstr(mrb, data_dir));
        /* the game's own font, else the app's shared one: the minisodes carry
         * no Fonts/ folder at all. The shared one is the romfs copy when the
         * app carries the game (the CIA), else the card's. */
        mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_font_file"),
                   mrb_str_new_cstr(mrb, shared_font()));
        /* the sounds, packed by tools/pack_audio.py: inside the app (build.ps1
         * puts work/audio/audio.ttma in romfs) for the main game, else a pack
         * beside a minisode, else Audio/ in the game folder. A minisode's own
         * pack is looked for first, but its shared soundtrack is already in the
         * app's pack (tools/pack_minisodes.py leaves those files out), so the
         * app's pack comes first and Audio/ covers only what it lacks. */
        char pack[320];
        const char* use = NULL;
        if (!game->main_game) {
            snprintf(pack, sizeof(pack), "%saudio.ttma", game->dir);
            if (file_exists(pack)) use = pack;
        }
        if (!use) {
            if (file_exists("romfs:/audio.ttma")) use = "romfs:/audio.ttma";
            else if (file_exists(APP_DIR "audio.ttma")) use = APP_DIR "audio.ttma";
        }
        if (use) mrb_gv_set(mrb, mrb_intern_lit(mrb, "$ttm_audio_pack"), mrb_str_new_cstr(mrb, use));

        FILE* f = fopen("romfs:/main.rb", "rb");
        if (!f) {
            ensure_console();
            printf("romfs:/main.rb missing\n");
            wait_for_start();
        } else {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* src = (char*)malloc((size_t)n + 1);
            fread(src, 1, (size_t)n, f);
            fclose(f);
            src[n] = 0;
            mrb_ccontext* cxt = mrb_ccontext_new(mrb);
            mrb_ccontext_filename(mrb, cxt, "main.rb");
            /* while the game runs the bottom screen is the game's menu;
             * the console comes back for an error */
            ttm_bottom_start();
            console_on = 0;
            mrb_load_nstring_cxt(mrb, src, (size_t)n, cxt);
            ttm_bottom_stop();
            console_on = 1;
            mrb_ccontext_free(mrb, cxt);
            free(src);
            if (mrb->exc) {
                if (mrb_obj_is_kind_of(mrb, mrb_obj_value(mrb->exc), mrb_class_get(mrb, "TTMQuit"))) {
                    mrb->exc = NULL;
                } else {
                    report_exception(mrb);
                    wait_for_start();
                }
            }
        }
        ttm_audio3ds_exit();    /* before mrb_close: the thread reads the pack's index */
        /* closes the archive and the audio pack (their gems' final hooks):
         * no file may stay open past romfsExit, or newlib's exit closes it
         * through a device already gone -- the console's exception screen on
         * Exit Game > Shutdown, run 12 */
        mrb_close(mrb);
    }

cleanup:
    if (log_file) fclose(log_file);
    romfsExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
