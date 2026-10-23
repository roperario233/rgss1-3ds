/*
 * 3ds.h -- SYNTAX-CHECK STUB ONLY.  Not the real libctru API.
 *
 * The real headers come from devkitPro (libctru). This file exists so the app
 * source can be parsed by a host compiler (gcc -fsyntax-only) on a machine
 * without the 3DS toolchain, which catches typos, brace mistakes and wrong
 * argument counts. It is never compiled into the app.
 *
 *   gcc -fsyntax-only -std=gnu11 -I 3ds-app/test/stubs 3ds-app/source/main.c
 */

#ifndef STUB_3DS_H
#define STUB_3DS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  Result;

typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;

#define KEY_A       (1u << 0)
#define KEY_B       (1u << 1)
#define KEY_START   (1u << 3)
#define KEY_DLEFT   (1u << 6)
#define KEY_DRIGHT  (1u << 7)
#define KEY_LEFT    (1u << 10)
#define KEY_RIGHT   (1u << 11)

#define R_SUCCEEDED(res) ((res) == 0)

/* services used by the test app */
void   osSetSpeedupEnable(bool enable);
u64    osGetTime(void);

/* memory regions (real enum lives in 3ds/svc.h, the accessors in 3ds/os.h) */
typedef enum {
    MEMREGION_ALL         = 0,
    MEMREGION_APPLICATION = 1,
    MEMREGION_SYSTEM      = 2,
    MEMREGION_BASE        = 3,
} MemRegion;
u32    osGetMemRegionSize(MemRegion region);
u32    osGetMemRegionUsed(MemRegion region);
u32    osGetMemRegionFree(MemRegion region);

Result cfguInit(void);
void   cfguExit(void);
Result CFGU_GetSystemModel(u8* model);
Result romfsInit(void);
Result romfsExit(void);
bool   aptMainLoop(void);
void   hidScanInput(void);
u32    hidKeysDown(void);
void*  linearAlloc(size_t size);
void   linearFree(void* ptr);
void   gfxInitDefault(void);
void   gfxExit(void);

#endif
