/*
 * fileio.c -- the game's own files on real storage: the SD card on the 3DS, the
 * host's disk on the PC.
 *
 * mruby-ttm-core carries no mruby-io (the console build has no OS layer), so
 * RGSS's File/Dir are built on these instead (rgss/compat.rb):
 *
 *   TTM.file_write(path, data)  create the folders, write the bytes -> count/nil
 *   TTM.file_size(path)         bytes, or nil -- nil also means "not there"
 *   TTM.file_delete(path)       true, or nil if it was not there
 *   TTM.file_mtime(path)        seconds since the epoch, or nil
 *   TTM.dir?(path)              true when the path is a folder
 *   TTM.dir_mkdir(path)         true (already there counts), or nil
 *   TTM.dir_entries(path)       the names in a folder, or nil
 *
 * Reading a whole file is TTM.read_file (ttm_core.c), which these share nothing
 * with on purpose: that one also reads the archive, the font and the romfs Ruby,
 * from paths the game never sees.
 *
 * Paths are the game's own -- "save/save4.rxdata", "music.txt" -- and
 * rgss/compat.rb puts $ttm_data_dir in front of every one, so the game can only
 * write where it should: on the console that is the app's folder on the SD card
 * (main.c sets "sdmc:/3ds/ttm3ds/"), on the PC a scratch folder the runners set.
 * A write makes its parent folders, because the game asks for "save/" and then
 * writes into it (102_Start Game.rb does Dir.mkdir, but only if it remembers to).
 *
 * Writes go through stdio, which is how the app already writes its log on the
 * SD card. A failure is nil, never a crash: a save that cannot be written must
 * not take the game with it.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#if defined(_WIN32)
#include <direct.h>
#endif
#if defined(__3DS__)
#include <3ds.h>
#endif

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/string.h>

/* is this path a folder? (stat, so a missing path is simply "no") */
static int is_dir(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (st.st_mode & S_IFDIR) != 0;
}

static int make_dir(const char* path)
{
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

/* mkdir -p: every parent of `path` that is missing, left to right. Only forward
 * slashes ("sdmc:/3ds/ttm3ds/save/"): the game's paths reach us through
 * TTMFiles.norm, which turns backslashes into slashes first. */
static void make_dirs(char* path)
{
    for (char* p = path + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (!is_dir(path)) make_dir(path);
        *p = '/';
    }
    if (!is_dir(path)) make_dir(path);
}

static mrb_value ttm_file_write(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_value data;
    mrb_get_args(mrb, "zS", &path, &data);

    char buf[1024];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return mrb_nil_value();
    memcpy(buf, path, len + 1);
    for (size_t i = 0; buf[i]; ++i) if (buf[i] == '\\') buf[i] = '/';
    char* slash = strrchr(buf, '/');
    if (slash && slash != buf) { *slash = '\0'; make_dirs(buf); *slash = '/'; }

    FILE* f = fopen(buf, "wb");
    if (!f) return mrb_nil_value();
    size_t n = RSTRING_LEN(data) > 0 ? fwrite(RSTRING_PTR(data), 1, (size_t)RSTRING_LEN(data), f) : 0;
    int ok = (fclose(f) == 0);
    if (!ok || n != (size_t)RSTRING_LEN(data)) return mrb_nil_value();
    return mrb_int_value(mrb, (mrb_int)n);
}

static mrb_value ttm_file_size(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    struct stat st;
    if (stat(path, &st) != 0 || (st.st_mode & S_IFDIR)) return mrb_nil_value();
    return mrb_int_value(mrb, (mrb_int)st.st_size);
}

static mrb_value ttm_file_mtime(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    struct stat st;
    if (stat(path, &st) != 0) return mrb_nil_value();
#if defined(__3DS__)
    /* the SD card's stat leaves st_mtime at 0; the archive keeps the time */
    if (st.st_mtime == 0) {
        u64 t;
        if (R_SUCCEEDED(archive_getmtime(path, &t))) return mrb_int_value(mrb, (mrb_int)t);
    }
#endif
    return mrb_int_value(mrb, (mrb_int)st.st_mtime);
}

static mrb_value ttm_file_delete(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    if (remove(path) != 0) return mrb_nil_value();
    return mrb_true_value();
}

static mrb_value ttm_dir_p(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    return mrb_bool_value(is_dir(path) != 0);
}

static mrb_value ttm_dir_mkdir(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    char buf[1024];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return mrb_nil_value();
    memcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';   /* "save/" */
    make_dirs(buf);
    return mrb_bool_value(is_dir(buf) != 0);
}

static mrb_value ttm_dir_entries(mrb_state* mrb, mrb_value self)
{
    const char* path;
    mrb_get_args(mrb, "z", &path);
    DIR* d = opendir(path);
    if (!d) return mrb_nil_value();
    mrb_value a = mrb_ary_new(mrb);
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        const char* name = e->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, name));
    }
    closedir(d);
    return a;
}

void ttm_init_fileio(mrb_state* mrb, struct RClass* ttm)
{
    mrb_define_module_function(mrb, ttm, "file_write", ttm_file_write, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, ttm, "file_size", ttm_file_size, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "file_mtime", ttm_file_mtime, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "file_delete", ttm_file_delete, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "dir?", ttm_dir_p, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "dir_mkdir", ttm_dir_mkdir, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, ttm, "dir_entries", ttm_dir_entries, MRB_ARGS_REQ(1));
}
