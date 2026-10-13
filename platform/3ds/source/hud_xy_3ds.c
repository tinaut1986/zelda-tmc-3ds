/* The X and Y item buttons' tiles, written into OBJ VRAM only while the top
 * screen renders (port/port_hud_xy.h, PLAN P2.6). */

#include "port_hud_xy.h"
#include "global.h"
#include "vram.h"
#include "port_gba_mem.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* src/ui.c: the HUD's ammo digits, 8 words a tile (tens 0-9, then ones 0-9). */
extern u32 gUnk_085C4620[];

enum {
    OAM_ENTRIES = 128,
    MAX_OBJS = 16,
    OBJ_VRAM = 0x10000,
    BUTTON_TILE = 0x100, /* the A bubble, sprite 505 frame 0 (gfx group 16) */
};

typedef struct {
    uint8_t index;
    uint16_t attr[3];
} HudXYObj;

typedef struct {
    HudXYObj objs[MAX_OBJS];
    unsigned objCount;
    bool bubble[PORT_HUD_XY_SLOTS];
    const uint8_t* iconTiles[PORT_HUD_XY_SLOTS];
    uint8_t iconCount[PORT_HUD_XY_SLOTS];
    int ammo[PORT_HUD_XY_SLOTS];
} HudXYFrame;

static HudXYFrame sDrawing;   /* the sprites being drawn this tick */
static HudXYFrame sCommitted; /* the ones in OAM now */

static uint8_t sSavedVram[PORT_HUD_XY_TILE_COUNT * 32u];
static uint16_t sSavedAttr0[MAX_OBJS];
static enum { RENDER_NONE, RENDER_TILES, RENDER_HIDDEN } sRenderState;

void Port_HudXY_BeginFrame(void) {
    memset(&sDrawing, 0, sizeof(sDrawing));
}

static void AddObjs(unsigned first, unsigned end) {
    for (unsigned i = first; i < end && i < OAM_ENTRIES && sDrawing.objCount < MAX_OBJS; ++i) {
        HudXYObj* obj = &sDrawing.objs[sDrawing.objCount++];
        const uint16_t* attr = (const uint16_t*)&gOAMControls.oam[i];
        obj->index = (uint8_t)i;
        obj->attr[0] = attr[0];
        obj->attr[1] = attr[1];
        obj->attr[2] = attr[2];
    }
}

void Port_HudXY_AddBubble(unsigned slot, unsigned first, unsigned end) {
    if (slot >= PORT_HUD_XY_SLOTS) return;
    sDrawing.bubble[slot] = true;
    AddObjs(first, end);
}

void Port_HudXY_AddIcon(unsigned slot, unsigned first, unsigned end, const void* tiles, unsigned numTiles,
                        int count) {
    if (slot >= PORT_HUD_XY_SLOTS || tiles == NULL || numTiles == 0 || numTiles > PORT_HUD_XY_ICON_MAX_TILES) return;
    sDrawing.iconTiles[slot] = (const uint8_t*)tiles;
    sDrawing.iconCount[slot] = (uint8_t)numTiles;
    sDrawing.ammo[slot] = count;
    AddObjs(first, end);
}

void Port_HudXY_CommitOam(void) {
    sCommitted = sDrawing;
}

/* ---- the tiles ---- */

static unsigned GetPixel(const uint8_t* tiles, int x, int y) {
    const uint8_t b = tiles[((y >> 3) * 2 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];
    return (x & 1) ? b >> 4 : b & 15u;
}

static void SetPixel(uint8_t* tiles, int x, int y, unsigned c) {
    uint8_t* b = &tiles[((y >> 3) * 2 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];
    *b = (x & 1) ? (uint8_t)((*b & 0x0f) | (c << 4)) : (uint8_t)((*b & 0xf0) | c);
}

/* The letter of the A bubble sits in columns 7-10, rows 6-10 (4x5, drawn in the
 * outline colour on the face colour). X and Y in the same hand. */
static const char* const kLetters[PORT_HUD_XY_SLOTS][5] = {
    { "#..#", "#..#", ".##.", "#..#", "#..#" },
    { "#..#", "#..#", ".##.", ".##.", ".##." },
};

static void BuildBubble(uint8_t* out, unsigned slot) {
    memcpy(out, (const uint8_t*)gVram + OBJ_VRAM + BUTTON_TILE * 32u, 4u * 32u);
    const unsigned ink = GetPixel(out, 8, 6);  /* the top of the A */
    const unsigned face = GetPixel(out, 7, 5); /* just above it */
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 4; ++x)
            SetPixel(out, 7 + x, 6 + y, kLetters[slot][y][x] == '#' ? ink : face);
}

static bool OamCommitted(void) {
    if (sCommitted.objCount == 0) return false;
    for (unsigned i = 0; i < sCommitted.objCount; ++i) {
        const HudXYObj* obj = &sCommitted.objs[i];
        const uint16_t* attr = &gOamMem[obj->index * 4u];
        if (attr[0] != obj->attr[0] || attr[1] != obj->attr[1] || attr[2] != obj->attr[2]) return false;
    }
    return true;
}

static bool IsOurs(unsigned index) {
    for (unsigned i = 0; i < sCommitted.objCount; ++i)
        if (sCommitted.objs[i].index == index) return true;
    return false;
}

/* Does any other sprite in OAM use the tiles we are about to write? */
static bool OthersUseOurTiles(void) {
    static const uint8_t widths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
    static const uint8_t heights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
    const bool oneD = (gIoMem[0] & 0x40u) != 0; /* DISPCNT OBJ 1D mapping */
    for (unsigned i = 0; i < OAM_ENTRIES; ++i) {
        if (IsOurs(i)) continue;
        const uint16_t attr0 = gOamMem[i * 4u], attr1 = gOamMem[i * 4u + 1u], attr2 = gOamMem[i * 4u + 2u];
        if ((attr0 & 0x300u) == 0x200u) continue; /* disabled */
        const unsigned shape = attr0 >> 14, size = attr1 >> 14;
        if (shape >= 3u) continue;
        const unsigned scale = (attr0 & 0x2000u) ? 2u : 1u;
        const unsigned w = widths[shape][size] / 8u, h = heights[shape][size] / 8u;
        const unsigned base = attr2 & 0x3ffu;
        for (unsigned r = 0; r < h; ++r) {
            const unsigned rowStart = oneD ? base + r * w * scale : base + r * 32u;
            const unsigned rowEnd = rowStart + w * scale;
            if (rowStart < PORT_HUD_XY_TILE_FIRST + PORT_HUD_XY_TILE_COUNT && rowEnd > PORT_HUD_XY_TILE_FIRST)
                return true;
        }
    }
    return false;
}

void Port_HudXY_BeginRender(void) {
    sRenderState = RENDER_NONE;
    if (!OamCommitted()) return;
    if (OthersUseOurTiles()) {
        /* A crowded scene took the top of OBJ VRAM: skip X/Y this frame. */
        for (unsigned i = 0; i < sCommitted.objCount; ++i) {
            uint16_t* attr0 = &gOamMem[sCommitted.objs[i].index * 4u];
            sSavedAttr0[i] = *attr0;
            *attr0 = 0x2A0; /* what CopyOAM writes into an unused entry */
        }
        sRenderState = RENDER_HIDDEN;
        return;
    }
    uint8_t* vram = (uint8_t*)gVram + OBJ_VRAM + PORT_HUD_XY_TILE_FIRST * 32u;
    memcpy(sSavedVram, vram, sizeof(sSavedVram));
    for (unsigned slot = 0; slot < PORT_HUD_XY_SLOTS; ++slot) {
        if (sCommitted.bubble[slot])
            BuildBubble((uint8_t*)gVram + OBJ_VRAM + PORT_HUD_XY_BUBBLE_TILE(slot) * 32u, slot);
        if (sCommitted.iconTiles[slot] != NULL) {
            uint8_t* icon = (uint8_t*)gVram + OBJ_VRAM + PORT_HUD_XY_ICON_TILE(slot) * 32u;
            memcpy(icon, sCommitted.iconTiles[slot], sCommitted.iconCount[slot] * 32u);
            const int count = sCommitted.ammo[slot];
            if (count >= 0 && sCommitted.iconCount[slot] >= 2) {
                unsigned tens = (unsigned)count / 10u;
                if (tens > 9u) tens = 9u;
                memcpy(icon, gUnk_085C4620 + tens * 8u, 32u);
                memcpy(icon + 32, gUnk_085C4620 + ((unsigned)count % 10u + 10u) * 8u, 32u);
            }
        }
    }
    sRenderState = RENDER_TILES;
}

void Port_HudXY_EndRender(void) {
    if (sRenderState == RENDER_TILES) {
        memcpy((uint8_t*)gVram + OBJ_VRAM + PORT_HUD_XY_TILE_FIRST * 32u, sSavedVram, sizeof(sSavedVram));
    } else if (sRenderState == RENDER_HIDDEN) {
        for (unsigned i = 0; i < sCommitted.objCount; ++i)
            gOamMem[sCommitted.objs[i].index * 4u] = sSavedAttr0[i];
    }
    sRenderState = RENDER_NONE;
}
