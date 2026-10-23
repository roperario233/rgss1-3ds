/*
 * citro2d.h -- SYNTAX-CHECK STUB ONLY.  Not the real citro2d/citro3d API.
 *
 * Declares exactly the symbols main.c uses so a host compiler can parse the
 * source without devkitPro installed. Never compiled into the app.
 */

#ifndef STUB_CITRO2D_H
#define STUB_CITRO2D_H

#include <3ds.h>

typedef enum { GPU_RGBA8 } GPU_TEXCOLOR;
typedef enum { GPU_NEAREST = 0, GPU_LINEAR = 1 } GPU_TEXTURE_FILTER_PARAM;
typedef enum { GPU_CLAMP_TO_EDGE = 0 } GPU_TEXTURE_WRAP_PARAM;

typedef struct { void* data; u16 width, height; } C3D_Tex;
typedef struct { int w, h; } C3D_RenderTarget;

/* matches tex3ds.h / c2d/base.h in current devkitPro */
typedef struct Tex3DS_SubTexture {
    u16   width;
    u16   height;
    float left, top, right, bottom;
} Tex3DS_SubTexture;

typedef struct { C3D_Tex* tex; const Tex3DS_SubTexture* subtex; } C2D_Image;
typedef struct { void* dummy; } C2D_ImageTint;

#define C3D_DEFAULT_CMDBUF_SIZE (0x80000)
#define C2D_DEFAULT_MAX_OBJECTS (4096)
#define C3D_FRAME_SYNCDRAW      0

u32  C2D_Color32(u8 r, u8 g, u8 b, u8 a);
void C3D_Init(size_t cmd_buf_size);
void C3D_Fini(void);
void C3D_FrameBegin(u8 flags);
void C3D_FrameEnd(u8 flags);
bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format);
void C3D_TexDelete(C3D_Tex* tex);
void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM mag,
                      GPU_TEXTURE_FILTER_PARAM min);
void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM x,
                    GPU_TEXTURE_WRAP_PARAM y);
void C3D_TexUpload(C3D_Tex* tex, const void* data);
void C3D_TexFlush(C3D_Tex* tex);
void C2D_Init(size_t max_objects);
void C2D_Fini(void);
void C2D_Prepare(void);
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget* target, u32 color);
void C2D_SceneBegin(C3D_RenderTarget* target);
void C2D_DrawImageAt(C2D_Image img, float x, float y, float z,
                     const C2D_ImageTint* tint, float scale_x, float scale_y);
void C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color);

#endif
