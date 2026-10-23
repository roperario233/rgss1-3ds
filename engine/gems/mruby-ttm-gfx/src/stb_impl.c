/*
 * stb_impl.c -- the one place the stb single-file libraries are compiled
 * (third_party/stb, pinned in COMMIT; public domain / MIT, see LICENSE).
 *   stb_image        PNG / JPEG / BMP decoding, from memory (the archive)
 *   stb_truetype     the game's TrueType font
 *   stb_image_write  PNG screenshots (PC build only)
 */

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"

/* stb keeps its last failure reason until the next failure sets one, and
 * stbi_info tries JPEG first: a PNG decode that then fails without naming a
 * reason reported "no SOI" (console runs 4 and 5). Clearing it before a
 * decode makes "no reason" visible as such. */
void ttm_stbi_clear_failure(void);
void ttm_stbi_clear_failure(void) { stbi__g_failure_reason = 0; }

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#ifndef __3DS__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#else
int gfx_save_png(const char* path);
int gfx_save_png(const char* path) { (void)path; return 0; }
int gfx_save_png_bottom(const char* path) { (void)path; return 0; }
#endif
