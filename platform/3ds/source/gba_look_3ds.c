/* The GBA look on the top screen (grid lines and console frame): see gba_look_3ds.h. */

#include "gba_look_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int Port_Config_GetGbaGrid(void);
extern bool Port_Config_GetGbaFrame(void);

enum { FRAME_UNPACKED_BYTES = 524288 }; /* 512 x 256 x 4 */

enum { SCREEN_W = 400, SCREEN_H = 240, TEX_W = 512, TEX_H = 256 };

/* ---- the grid ---- */

static C3D_Tex sMaskTex;
static bool sMaskReady, sMaskFailed;
/* What the mask was baked for (-1: nothing yet). */
static int sBaked[7] = { -1, -1, -1, -1, -1, -1, -1 }; /* level, x, y, w, h, srcW, srcH */

/* Byte offset of texel (x, y) in the 3DS tiled layout (8x8 tiles, Morton order inside each). */
static uint32_t SwizzleOffset(uint32_t x, uint32_t y) {
    const uint32_t tile = (y >> 3) * (TEX_W >> 3) + (x >> 3);
    const uint32_t in = (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) | ((y & 2u) << 2) |
                        ((x & 4u) << 2) | ((y & 4u) << 3);
    return tile * 64u + in;
}

/* Alpha of the black line over screen pixel (lx, ly) of the picture, 0 if none. A line is one
 * screen pixel on the first row of every other source row (`row` is the source row this pixel
 * falls in), so it follows the game's pixels at any scale; HIGH adds the same on the columns.
 * The strengths are the ones of ../mzm. */
static uint8_t GridAlpha(int level, int lx, int ly, int dw, int dh, int sw, int sh) {
    static const uint8_t kRow[4] = { 0, 55, 95, 95 };
    static const uint8_t kCol[4] = { 0, 0, 0, 95 };
    const int row = ly * sh / dh;
    const int prevRow = ly > 0 ? (ly - 1) * sh / dh : -1;
    if (row != prevRow && (row & 1) == 0) return kRow[level];
    if (kCol[level]) {
        const int col = lx * sw / dw;
        const int prevCol = lx > 0 ? (lx - 1) * sw / dw : -1;
        if (col != prevCol && (col & 1) == 0) return kCol[level];
    }
    return 0;
}

static bool MaskInit(void) {
    if (sMaskReady) return true;
    if (sMaskFailed) return false;
    /* One byte a texel: the alpha of a black tint (the environment supplies the black). */
    if (!C3D_TexInit(&sMaskTex, TEX_W, TEX_H, GPU_A8)) {
        sMaskFailed = true;
        return false;
    }
    C3D_TexSetFilter(&sMaskTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sMaskTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sMaskReady = true;
    return true;
}

static void MaskBake(int level, int x, int y, int w, int h, int sw, int sh) {
    uint8_t* data = (uint8_t*)sMaskTex.data;
    memset(data, 0, (size_t)TEX_W * TEX_H);
    for (int ly = 0; ly < h; ++ly) {
        for (int lx = 0; lx < w; ++lx) {
            const uint8_t a = GridAlpha(level, lx, ly, w, h, sw, sh);
            if (a) data[SwizzleOffset((uint32_t)(x + lx), (uint32_t)(y + ly))] = a;
        }
    }
    GSPGPU_FlushDataCache(data, (size_t)TEX_W * TEX_H);
    sBaked[0] = level; sBaked[1] = x; sBaked[2] = y; sBaked[3] = w; sBaked[4] = h;
    sBaked[5] = sw; sBaked[6] = sh;
}

/* A black tint whose alpha is the mask's. */
static void SetMaskEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, 0, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvColor(env, 0);
    C3D_TexEnvInit(C3D_GetTexEnv(1));
    C3D_TexEnvInit(C3D_GetTexEnv(2));
}

static void DrawGrid(int x, int y, int w, int h, int sw, int sh, int level) {
    if (sBaked[0] != level || sBaked[1] != x || sBaked[2] != y || sBaked[3] != w ||
        sBaked[4] != h || sBaked[5] != sw || sBaked[6] != sh)
        MaskBake(level, x, y, w, h, sw, sh);
    SetMaskEnv();
    /* Only the picture's part of the mask: the rest is empty. */
    const Tex3DS_SubTexture sub = {
        .width = (u16)w,
        .height = (u16)h,
        .left = (float)x / TEX_W,
        .right = (float)(x + w) / TEX_W,
        .top = 1.0f - (float)y / TEX_H,
        .bottom = 1.0f - (float)(y + h) / TEX_H,
    };
    const C2D_Image image = { .tex = &sMaskTex, .subtex = &sub };
    const C2D_DrawParams params = {
        .pos = { (float)x, (float)y, (float)w, (float)h },
        .center = { 0.0f, 0.0f }, .depth = 0.6f, .angle = 0.0f,
    };
    C2D_DrawImage(image, &params, NULL);
    C2D_Flush();
}

/* ---- the frame ---- */

enum FrameKind { FRAME_NONE = 0, FRAME_FULL, FRAME_SIDES };

static C3D_Tex sFrameTex;
static bool sFrameReady, sFrameFailed;

/* Standard LZ4 block decompressor: the artwork is stored compressed in the binary. */
static bool Lz4Decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    size_t s = 0, d = 0;
    while (d < dstLen && s < srcLen) {
        const uint8_t token = src[s++];
        size_t lit = token >> 4;
        if (lit == 15) {
            while (s < srcLen) {
                const uint8_t extra = src[s++];
                lit += extra;
                if (extra != 255) break;
            }
        }
        if (s + lit > srcLen || d + lit > dstLen) return false;
        memcpy(dst + d, src + s, lit);
        d += lit;
        s += lit;
        if (d >= dstLen) break;
        if (s + 2 > srcLen) return false;
        const size_t offset = (size_t)src[s] | ((size_t)src[s + 1] << 8);
        s += 2;
        if (offset == 0 || offset > d) return false;
        size_t match = (token & 0x0F) + 4;
        if (match == 19) {
            while (s < srcLen) {
                const uint8_t extra = src[s++];
                match += extra;
                if (extra != 255) break;
            }
        }
        if (d + match > dstLen) return false;
        for (size_t i = 0; i < match; ++i, ++d) dst[d] = dst[d - offset];
    }
    return d == dstLen;
}

/* The packed artwork, romfs:/gba-look-frame.lz4, is read, unpacked straight into the texture and
 * dropped again. */
static bool FrameInit(void) {
    if (sFrameReady) return true;
    if (sFrameFailed) return false;
    sFrameFailed = true; /* until it is all in */
    FILE* f = fopen("romfs:/gba-look-frame.lz4", "rb");
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* packed = size > 0 && size < FRAME_UNPACKED_BYTES ? malloc((size_t)size) : NULL;
    const bool read = packed != NULL && fread(packed, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!read || !C3D_TexInit(&sFrameTex, TEX_W, TEX_H, GPU_RGBA8)) {
        free(packed);
        return false;
    }
    const bool ok = Lz4Decompress(packed, (size_t)size, (uint8_t*)sFrameTex.data, FRAME_UNPACKED_BYTES);
    free(packed);
    if (!ok) {
        C3D_TexDelete(&sFrameTex);
        return false;
    }
    GSPGPU_FlushDataCache(sFrameTex.data, FRAME_UNPACKED_BYTES);
    /* Every piece goes to the screen one texel to a pixel. */
    C3D_TexSetFilter(&sFrameTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sFrameTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sFrameFailed = false;
    sFrameReady = true;
    return true;
}

/* The artwork is laid out for a 400x240 screen with the 240x160 picture's window at (80, 40);
 * the two 20-pixel strips that go beside a 360x240 picture are at x 416 and x 448. */
static enum FrameKind FrameFor(int x, int y, int w, int h) {
    if (w == 240 && h == 160 && x == 80 && y == 40) return FRAME_FULL;
    if (w == 360 && h == 240 && x == 20 && y == 0) return FRAME_SIDES;
    return FRAME_NONE;
}

/* One w x h piece of the artwork, from texel (srcX, srcY), drawn at screen (dstX, dstY). */
static void DrawFramePiece(int srcX, int srcY, int w, int h, int dstX, int dstY) {
    if (w <= 0 || h <= 0) return;
    const Tex3DS_SubTexture sub = {
        .width = (u16)w,
        .height = (u16)h,
        .left = (float)srcX / TEX_W,
        .right = (float)(srcX + w) / TEX_W,
        .top = 1.0f - (float)srcY / TEX_H,
        .bottom = 1.0f - (float)(srcY + h) / TEX_H,
    };
    const C2D_Image image = { .tex = &sFrameTex, .subtex = &sub };
    /* Under the scene pictures (0.5), which also use the border, and over nothing else. */
    C2D_DrawImageAt(image, (float)dstX, (float)dstY, 0.4f, NULL, 1.0f, 1.0f);
}

static void DrawFrame(enum FrameKind kind, int x, int y, int w, int h) {
    /* The texel's own colour and alpha. */
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    C3D_TexEnvInit(C3D_GetTexEnv(1));
    C3D_TexEnvInit(C3D_GetTexEnv(2));
    if (kind == FRAME_FULL) {
        /* Around the window, skipping its transparent middle. */
        DrawFramePiece(0, 0, x, SCREEN_H, 0, 0);
        DrawFramePiece(x + w, 0, SCREEN_W - (x + w), SCREEN_H, x + w, 0);
        DrawFramePiece(x, 0, w, y, x, 0);
        DrawFramePiece(x, y + h, w, SCREEN_H - (y + h), x, y + h);
    } else {
        DrawFramePiece(416, 0, 20, SCREEN_H, 0, 0);
        DrawFramePiece(448, 0, 20, SCREEN_H, SCREEN_W - 20, 0);
    }
    C2D_Flush();
}

bool GbaLook3DS_Draw(int drawX, int drawY, int drawW, int drawH, int srcW, int srcH) {
    int level = Port_Config_GetGbaGrid();
    if (level < 0 || level > 3) level = 0;
    enum FrameKind frame = Port_Config_GetGbaFrame() ? FrameFor(drawX, drawY, drawW, drawH) : FRAME_NONE;
    if (frame != FRAME_NONE && !FrameInit()) frame = FRAME_NONE;
    if (level != 0 && (drawW <= 0 || drawH <= 0 || srcW <= 0 || srcH <= 0 || !MaskInit())) level = 0;
    if (level == 0 && frame == FRAME_NONE) return false;

    /* The frame's quads go out under the environment they were queued with. */
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    /* Fully transparent texels are dropped before they read the target. */
    C3D_AlphaTest(true, GPU_GREATER, 0);
    if (frame != FRAME_NONE) DrawFrame(frame, drawX, drawY, drawW, drawH);
    if (level != 0) DrawGrid(drawX, drawY, drawW, drawH, srcW, srcH, level);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    return true;
}

void GbaLook3DS_Shutdown(void) {
    if (sMaskReady) C3D_TexDelete(&sMaskTex);
    if (sFrameReady) C3D_TexDelete(&sFrameTex);
    sMaskReady = sFrameReady = false;
    sMaskFailed = sFrameFailed = false;
    for (int i = 0; i < 7; ++i) sBaked[i] = -1;
}
