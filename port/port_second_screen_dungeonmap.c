#include "port_second_screen_color.h"
#include "port_second_screen_dungeonmap.h"

/*
 * Authentic dungeon-map rendering for the second screen, rebuilt from the
 * pause menu's own dungeon-map tab and decoded from ROM at runtime — no
 * baked pixels, same policy as the item icons in
 * port_second_screen_render.c.
 *
 * Sources in the decomp this mirrors:
 *   - Room shapes: DrawDungeonFeatures (src/common.c) — per-room 2bpp
 *     bitmaps at gMapData + DungeonLayout.mapDataOffset, one map pixel per
 *     16px game tile, placed by the room's RoomHeader.map_x/map_y inside a
 *     128x128 map-pixel floor. map_y's bits 11+ are the floor row
 *     (sub_0801DB94), so floors stack in y at 0x800-pixel strides.
 *   - Floor structure: gDungeonLayouts[dungeon][floor] room lists +
 *     gDungeonFloorMetadatas[dungeon].numFloors (src/common.c), display
 *     order top floor first — the same tables PauseMenu_Screen_5 pages
 *     through with the dpad.
 *   - Colors: BG palette bank 12, loaded for the map tab by palette group
 *     184 (gUnk_08128AD8[3] in src/data/figurineMenuData.c). Raw pixel 1 =
 *     wall, 3 -> 7 = doorways, 2 = floor fill recolored by room state
 *     (sub_0801DF78), and the current room's fill (index 8) pulses because
 *     PauseMenu_Screen_5 rotates bank 12's colors 8..15 every 8 ticks.
 *   - Markers: DrawDungeonMap + DrawDungeonMapActually (src/common.c,
 *     src/menu/pauseMenu.c) — DrawDirect frames 0x7d / 0x7e (Link on
 *     his floor / on another), 0x80 (chest), 0x7f (boss face), 0x81
 *     (re-entry arrow) of the direct sprite sheet, decoded from
 *     gFrameObjLists + the OBJ tiles the map screen's gfx groups load in
 *     the save's language. The floor list's own frames (floor names,
 *     cursor, Link's and the boss's floor icons) go through the same
 *     decoder for the caller (Port_SecondScreenDungeonMap_DrawFrame).
 *
 * Reveal rules mirror the real map screen so the bottom screen never
 * cheats (DrawDungeonFeatures's `features` logic, exactly):
 *   - the room the player stands in always draws (pulsing fill),
 *   - visited rooms draw with the lit fill (color 3): visited is the
 *     save's own flag, IsRoomVisited (src/common.c) — the local flag of
 *     the room's ROOM_VISIT_MARKER tile entity in its flag bank — read
 *     from the snapshot's gSave.flags copy, so it lasts across sessions
 *     and covers the floor's rooms of every area (boss arenas),
 *   - other rooms draw (dim fill, color 2) only when the dungeon MAP item
 *     is owned; a visited room keeps the lit fill with it too,
 *   - rooms flagged hidden (DungeonLayout.unk_2 & 1) never draw,
 *   - chest and boss markers draw only with the COMPASS, like the game,
 *     and DrawDungeonMap's flag tests drop them: a chest's marker once
 *     its local flag (TileEntity.localFlag in the room's flag bank,
 *     sub_0801DF10) is set, i.e. the chest is open; the boss skull once
 *     global flag dungeon_idx + 1 is set, i.e. the boss is beaten. The
 *     flags come from the snapshot's copy of gSave.flags,
 *   - the re-entry arrow and Link draw with or without the compass.
 *
 * Dungeon item bits: include/save.h's comment ("4: compass, 2: big key,
 * 1: small key") is wrong — per HasDungeonMap / HasDungeonCompass /
 * HasDungeonBigKey (src/gameUtils.c) the real encoding of
 * gSave.dungeonItems[] is bit 0 (1) = dungeon map, bit 1 (2) = compass,
 * bit 2 (4) = big key; small keys live in gSave.dungeonKeys[] instead.
 * dungeonItemBits carries that byte raw, so map = &1, compass = &2 here.
 *
 * Threading contract matches port_second_screen_worldmap.h: ROM-constant
 * data only (gMapData, layout tables, the raw palette/gfx/frame accessors
 * appended to src/common.c and port/port_draw.c); every live value arrives
 * in the snapshot. The canvas below makes Draw single-caller — fine, it
 * only ever runs on the second-screen render thread.
 */

#include "area.h"
#include "common.h"
#include "flags.h"
#include "region.h"
#include "room.h"

#include "port_offset_remap.h"
#include "port_rom.h"
#include "port_second_screen_state.h" /* SECOND_SCREEN_SAVE_FLAG_BYTES */

#include <string.h>

/* src/common.c (tables live at the end of that file; only
 * gDungeonFloorMetadatas has a header declaration). */
extern const DungeonLayout* const* const gDungeonLayouts[];
/* src/common.c: a layout entry's local flag bank (0x300 for unk_3 == 1,
 * else the area's own bank). ROM-constant tables only. */
extern u32 sub_0801DF10(const DungeonLayout* layout);

/* ReadBit over the snapshot's gSave.flags copy, i.e. what
 * CheckLocalFlagByBank / CheckGlobalFlag (src/playerUtils.c,
 * src/flags.c) answer on the game thread. No copy = flag clear, which
 * keeps every marker. */
static int SaveFlagSet(const uint8_t* saveFlags, uint32_t bit) {
    if (saveFlags == NULL || (bit >> 3) >= SECOND_SCREEN_SAVE_FLAG_BYTES) {
        return 0;
    }
    return (saveFlags[bit >> 3] >> (bit & 7u)) & 1u;
}

/* IsRoomVisited (src/common.c) over the flags copy: the first
 * ROOM_VISIT_MARKER tile entity's local flag; a room without one is never
 * visited. */
static int RoomVisited(const DungeonLayout* lyt, const uint8_t* saveFlags) {
    const TileEntity* te = (const TileEntity*)GetRoomProperty(lyt->area, lyt->room, 3);
    uint32_t n;
    if (te == NULL) {
        return 0;
    }
    for (n = 0; n < 256 && te[n].type != 0; n++) {
        if (te[n].type == ROOM_VISIT_MARKER) {
            return SaveFlagSet(saveFlags, sub_0801DF10(lyt) + te[n].localFlag);
        }
    }
    return 0;
}


/* Raw-data accessors: src/common.c (palette groups, gfx groups, room
 * headers) and port/port_draw.c (frame pieces, OAM size table). */
extern const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors);
extern const u8* Port_GetRawGfxSpanForVramLang(u32 group, u32 vramAddr, u32 numBytes, u32 saveLanguage);
extern const RoomHeader* Port_GetRoomHeaderSafe(u32 area, u32 room);
extern const u8* Port_GetDirectSpriteFrame(u32 spriteIndex, u32 frameIndex, u32* outMaxPieces);
extern const u8* Port_GetSpriteSizeTable(void);

/* gDungeonLayouts has entries for "no dungeon" + the six real dungeons. */
#define DUNGEON_COUNT 7

/* One floor of the in-game map is a 128x128 map-pixel image. */
#define MAP_PX 128

/* DrawDirect sheet holding the map markers (include/subtask.h's
 * DRAW_DIRECT_SPRITE_INDEX). */
#define DIRECT_SPRITE_INDEX (REGION_IS_EU ? 0x1fau : 0x1fbu)

/* Marker frames, from DrawDungeonMapActually's switch over the
 * DrawDungeonMap objects (src/menu/pauseMenu.c, src/common.c):
 * DMO_TYPE_PLAYER 0x7d on Link's own floor, 0x7e on the others;
 * DMO_TYPE_CHEST 0x80; DMO_TYPE_ENTRY (the re-entry arrow) 0x81;
 * DMO_TYPE_BOSS 0x7f (none while sub_080A5F24 says no). */
#define FRAME_MARKER_PLAYER 0x7du
#define FRAME_MARKER_PLAYER_OTHER_FLOOR 0x7eu
#define FRAME_MARKER_BOSS 0x7fu
#define FRAME_MARKER_CHEST 0x80u
#define FRAME_MARKER_ENTRY 0x81u

/* DrawDungeonMap's object buffer is 0x400 bytes of 3-byte objects. */
#define MAX_MARKERS 341

/* Palette groups covering the dungeon-map tab, most recently loaded first:
 * 184 arrives with the tab itself (gUnk_08128AD8[3]), 181 and 11 with the
 * pause menu (sub_080A4D34 -> LoadPaletteGroup(0xb5), LoadGfxGroups). */
static const u8 kMapScreenPaletteGroups[] = { 184, 181, 11 };

/* The room-shape colors sit in BG palette bank 12 (PauseMenu_Screen_5
 * animates exactly that bank), which group 184's second chain entry loads. */
#define MAP_BG_PALETTE_GROUP 184u
#define MAP_BG_PALETTE_BANK 12u

/* Gfx groups whose OBJ-VRAM loads are live on the dungeon-map tab, most
 * recently loaded first: 129 (sub_080A5C44), 92 (the tab's gfx group), 86
 * (sub_080A4D34's heart-gfx group — its OBJ spans are identical across the
 * 86/87/88 health variants), 16 + 23 (LoadGfxGroups). The item-icon groups
 * (24/25/27..29) are skipped: inventory-conditional, and markers don't
 * live in their 0x6011800 window. */
static const u8 kMapScreenObjGfxGroups[] = { 129, 92, 86, 16, 23 };

/* Marker sprites can overhang their room by a few map pixels; give the
 * compose canvas a margin so nothing clips before scaling. */
#define CANVAS_MARGIN 16
#define CANVAS_W (MAP_PX + 2 * CANVAS_MARGIN)

/* Single-caller compose buffer (second-screen render thread only): one
 * floor at native map-pixel scale, 0 = transparent. */
static uint32_t sCanvas[CANVAS_W * CANVAS_W];

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    return Port_SecondScreen_Rgb555(c); /* COLOURS, like the top screen */
}

static const DungeonLayout* GetFloorList(uint8_t dungeonIdx, uint32_t floor) {
    if (dungeonIdx == 0 || dungeonIdx >= DUNGEON_COUNT) {
        return NULL;
    }
    if (floor >= gDungeonFloorMetadatas[dungeonIdx].numFloors) {
        return NULL;
    }
    return gDungeonLayouts[dungeonIdx][floor];
}

/* OBJ tile data (32 bytes, 4bpp) as the map screen's VRAM would hold it.
 * Walking most-recent-group-first reproduces "later loads overwrite".
 * Language-gated entries count when LoadGfxGroup would load them for
 * `saveLanguage` (gSaveHeader->language): groups 129 and 92 carry the
 * floor names (1F/B1, PB/S.1, ...) that way, one span per language. */
static const uint8_t* MapScreenObjTile(uint32_t tileNo, uint32_t saveLanguage) {
    uint32_t i;
    if (tileNo >= 1024) {
        return NULL;
    }
    for (i = 0; i < sizeof(kMapScreenObjGfxGroups); i++) {
        const uint8_t* p = Port_GetRawGfxSpanForVramLang(kMapScreenObjGfxGroups[i], 0x6010000u + tileNo * 32u,
                                                         32u, saveLanguage);
        if (p != NULL) {
            return p;
        }
    }
    return NULL;
}

static const uint16_t* MapScreenObjPalette(uint32_t row) {
    uint32_t i;
    for (i = 0; i < sizeof(kMapScreenPaletteGroups); i++) {
        u32 numColors = 0;
        const uint8_t* p =
            Port_GetRawPaletteGroupBankData(kMapScreenPaletteGroups[i], 16u + (row & 15u), &numColors);
        if (p != NULL && numColors >= 16) {
            return (const uint16_t*)p;
        }
    }
    return NULL;
}

static void CanvasPlot(int32_t x, int32_t y, uint32_t rgba) {
    x += CANVAS_MARGIN;
    y += CANVAS_MARGIN;
    if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_W) {
        return;
    }
    sCanvas[(size_t)y * CANVAS_W + (size_t)x] = rgba;
}

/* Where a walked frame's pixels go: the map canvas, or the caller's
 * buffer at an integer scale. */
typedef struct {
    uint32_t* pixels; /* NULL = the map canvas */
    int32_t bufW, bufH, stride, scale;
    uint32_t plotted;
} FrameSink;

static void SinkPlot(FrameSink* sink, int32_t ax, int32_t ay, int32_t dx, int32_t dy, uint32_t rgba) {
    int32_t sx, sy;
    sink->plotted++;
    if (sink->pixels == NULL) {
        CanvasPlot(ax + dx, ay + dy, rgba);
        return;
    }
    for (sy = 0; sy < sink->scale; sy++) {
        int32_t y = ay + dy * sink->scale + sy;
        if (y < 0 || y >= sink->bufH) {
            continue;
        }
        for (sx = 0; sx < sink->scale; sx++) {
            int32_t x = ax + dx * sink->scale + sx;
            if (x >= 0 && x < sink->bufW) {
                sink->pixels[(size_t)y * (size_t)sink->stride + (size_t)x] = rgba;
            }
        }
    }
}

/* One DrawDirect frame anchored like the game anchors its map-screen
 * sprites (gOamCmd position = the anchor; pieces then offset around it
 * minus the OAM size-table anchor — RenderSpritePieces in
 * port/port_draw.c, for the flags the map screen uses: no affine, no flip
 * from the command, extra/palette override 0). */
static void WalkDirectFrame(FrameSink* sink, int32_t ax, int32_t ay, uint32_t frameIndex, uint32_t saveLanguage) {
    const uint8_t* sizeTab = Port_GetSpriteSizeTable();
    u32 maxPieces = 0;
    const uint8_t* fd = Port_GetDirectSpriteFrame(DIRECT_SPRITE_INDEX, frameIndex, &maxPieces);
    uint32_t count;
    uint32_t i;

    if (fd == NULL || sizeTab == NULL) {
        return;
    }
    count = fd[0];
    fd++;
    if (count > maxPieces) {
        count = maxPieces;
    }
    for (i = 0; i < count; i++, fd += 5) {
        int32_t xoff = (int8_t)fd[0];
        int32_t yoff = (int8_t)fd[1];
        uint32_t shapeInfo = fd[2];
        /* attr2 layout: tile number low 10 bits, palette row top 4 — the
         * frame data bakes the palette in (command extra is 0 here). */
        uint32_t attr2 = (uint32_t)fd[3] | ((uint32_t)fd[4] << 8);
        uint32_t tileNo = attr2 & 0x3FFu;
        uint32_t palRow = attr2 >> 12;
        /* Size/anchor entry: sub-table 0 (no flip flags on the command),
         * indexed by shape (bits 7-6) and size (bits 5-4). */
        const uint8_t* se = &sizeTab[(shapeInfo & 0xF0u) >> 2];
        int32_t px = xoff - (int32_t)se[0];
        int32_t py = yoff - (int32_t)se[1];
        int32_t wpx = se[2];
        int32_t hpx = se[3];
        int32_t tilesW = wpx / 8;
        int32_t tilesH = hpx / 8;
        int32_t hflip = (shapeInfo & 4u) != 0; /* piece-local flips */
        int32_t vflip = (shapeInfo & 8u) != 0;
        const uint16_t* pal = MapScreenObjPalette(palRow);
        int32_t tx, ty, sx, sy;

        if (pal == NULL || tilesW <= 0 || tilesH <= 0) {
            continue;
        }
        for (ty = 0; ty < tilesH; ty++) {
            for (tx = 0; tx < tilesW; tx++) {
                /* OBJ 1D mapping (displayControl bit 0x40 is set on every
                 * pause-menu screen): consecutive tiles, row-major. */
                const uint8_t* tile = MapScreenObjTile(tileNo + (uint32_t)(ty * tilesW + tx), saveLanguage);
                if (tile == NULL) {
                    continue;
                }
                for (sy = 0; sy < 8; sy++) {
                    for (sx = 0; sx < 8; sx++) {
                        uint8_t packed = tile[sy * 4 + sx / 2];
                        uint8_t colorIndex = (sx & 1) ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0Fu);
                        int32_t ox, oy;
                        if (colorIndex == 0) {
                            continue; /* 0 == transparent */
                        }
                        ox = tx * 8 + sx;
                        oy = ty * 8 + sy;
                        SinkPlot(sink, ax, ay, px + (hflip ? wpx - 1 - ox : ox), py + (vflip ? hpx - 1 - oy : oy),
                                 Rgb555ToRgba8888(pal[colorIndex]));
                    }
                }
            }
        }
    }
}

/* A marker on the map canvas, at map-pixel scale. */
static void DrawMarkerFrame(int32_t mapX, int32_t mapY, uint32_t frameIndex, uint32_t saveLanguage) {
    FrameSink sink;
    memset(&sink, 0, sizeof(sink));
    WalkDirectFrame(&sink, mapX, mapY, frameIndex, saveLanguage);
}

int Port_SecondScreenDungeonMap_DrawFrame(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t x,
                                          int32_t y, int32_t scale, uint32_t frameIndex, uint8_t saveLanguage) {
    FrameSink sink;
    if (pixels == NULL) {
        return 0;
    }
    memset(&sink, 0, sizeof(sink));
    sink.pixels = pixels;
    sink.bufW = bufW;
    sink.bufH = bufH;
    sink.stride = stride;
    sink.scale = scale < 1 ? 1 : scale;
    WalkDirectFrame(&sink, x, y, frameIndex, saveLanguage);
    return sink.plotted != 0;
}

int Port_SecondScreenDungeonMap_FrameBounds(uint32_t frameIndex, int32_t* x0, int32_t* y0, int32_t* x1, int32_t* y1) {
    const uint8_t* sizeTab = Port_GetSpriteSizeTable();
    u32 maxPieces = 0;
    const uint8_t* fd = Port_GetDirectSpriteFrame(DIRECT_SPRITE_INDEX, frameIndex, &maxPieces);
    int32_t minX = 0x7FFF, minY = 0x7FFF, maxX = -0x7FFF, maxY = -0x7FFF;
    uint32_t count, i;

    if (fd == NULL || sizeTab == NULL) {
        return 0;
    }
    count = fd[0];
    fd++;
    if (count > maxPieces) {
        count = maxPieces;
    }
    for (i = 0; i < count; i++, fd += 5) {
        /* WalkDirectFrame's piece placement, rectangles only. */
        const uint8_t* se = &sizeTab[(fd[2] & 0xF0u) >> 2];
        int32_t px = (int8_t)fd[0] - (int32_t)se[0];
        int32_t py = (int8_t)fd[1] - (int32_t)se[1];
        if (se[2] == 0 || se[3] == 0) {
            continue;
        }
        if (px < minX) minX = px;
        if (py < minY) minY = py;
        if (px + se[2] > maxX) maxX = px + se[2];
        if (py + se[3] > maxY) maxY = py + se[3];
    }
    if (maxX <= minX || maxY <= minY) {
        return 0;
    }
    *x0 = minX;
    *y0 = minY;
    *x1 = maxX;
    *y1 = maxY;
    return 1;
}

/* sub_080A5F24 (src/menu/pauseMenu.c): the boss marks stay off in dungeon
 * 6 once local flag 0x7b of bank 10 is set. */
static int BossMarksShown(const SecondScreenSnapshot* snap) {
    return !(snap->dungeonIdx == 6 && SaveFlagSet(snap->saveFlags, FLAG_BANK_10 + 0x7bu));
}

int Port_SecondScreenDungeonMap_FloorLabelFrame(uint8_t dungeonIdx, int32_t row) {
    if (dungeonIdx == 0 || dungeonIdx >= DUNGEON_COUNT || row < 0 ||
        row >= gDungeonFloorMetadatas[dungeonIdx].numFloors) {
        return -1;
    }
    /* DrawDungeonMapActually: highestFloor + 0x82 for the top row, one
     * frame less per row down. */
    return (int)gDungeonFloorMetadatas[dungeonIdx].highestFloor + 0x82 - row;
}

int Port_SecondScreenDungeonMap_LinkFloor(const SecondScreenSnapshot* snap) {
    return snap->dungeonMapY >> 11; /* sub_0801DB94 */
}

int Port_SecondScreenDungeonMap_BossFloor(const SecondScreenSnapshot* snap) {
    const DungeonFloorMetadata* md;
    if (snap->dungeonIdx == 0 || snap->dungeonIdx >= DUNGEON_COUNT || !(snap->dungeonItemBits & 2) ||
        !BossMarksShown(snap)) {
        return -1;
    }
    md = &gDungeonFloorMetadatas[snap->dungeonIdx];
    return (int)md->highestFloor - (int)md->unk_2;
}

/* One room's shape, DrawDungeonFeatures's inner loop: 2bpp bitmap, 4
 * pixels per byte MSB-first (sub_0801DF60), row stride (width+3)/4, raw
 * values recolored per sub_0801DF78 (0 stays clear, 1 stays 1, 2 becomes
 * the room-state color, 3 becomes 7). */
static void BlitRoomShape(const DungeonLayout* lyt, uint32_t features, const uint16_t* pal,
                          uint32_t pulseColorIdx) {
    const RoomHeader* hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
    const uint8_t* shape;
    uint32_t mapX, mapY, w, h, stride, x, y;

    if (hdr == NULL) {
        return;
    }
    shape = gMapData + Port_RemapMapOffset(lyt->mapDataOffset);
    mapX = hdr->map_x / 16u;
    mapY = (hdr->map_y & 0x7FFu) / 16u; /* in-floor position; bits 11+ are the floor row */
    w = hdr->pixel_width / 16u;
    h = hdr->pixel_height / 16u;
    stride = (w + 3u) / 4u;
    if (w == 0 || h == 0 || stride * h > 0x400u) { /* the game DMAs at most 0x400 bytes per room */
        return;
    }
    for (y = 0; y < h; y++) {
        const uint8_t* row = shape + y * stride;
        for (x = 0; x < w; x++) {
            uint32_t raw = (row[x >> 2] >> (2u * ((~x) & 3u))) & 3u;
            uint32_t colorIdx = raw;
            if (raw == 2u) {
                colorIdx = features;
            } else if (raw == 3u) {
                colorIdx = 7u;
            }
            if (colorIdx == 0) {
                continue;
            }
            if (colorIdx == 8u) {
                colorIdx = pulseColorIdx; /* the rotating current-room fill */
            }
            if (mapX + x >= MAP_PX || mapY + y >= MAP_PX) {
                continue;
            }
            CanvasPlot((int32_t)(mapX + x), (int32_t)(mapY + y), Rgb555ToRgba8888(pal[colorIdx & 15u]));
        }
    }
}

int Port_SecondScreenDungeonMap_GetInfo(uint8_t dungeonIdx, uint8_t area, uint8_t room,
                                        SecondScreenDungeonMapInfo* out) {
    uint32_t numFloors, f;
    int32_t found = -1;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (out == NULL || dungeonIdx == 0 || dungeonIdx >= DUNGEON_COUNT) {
        return 0;
    }
    numFloors = gDungeonFloorMetadatas[dungeonIdx].numFloors;
    if (numFloors == 0 || numFloors > 8) {
        return 0;
    }
    /* The floor lists are the game's own room->floor mapping; scan them in
     * display order (index 0 = top floor, same order PauseMenu_Screen_5
     * shows them). */
    for (f = 0; f < numFloors && found < 0; f++) {
        const DungeonLayout* lyt = gDungeonLayouts[dungeonIdx][f];
        for (; lyt != NULL && lyt->area != 0; lyt++) {
            if (lyt->area == area && lyt->room == room) {
                found = (int32_t)f;
                break;
            }
        }
    }
    if (found < 0) {
        /* Rooms outside the lists (side chambers) still carry their floor
         * in map_y's bits 11+ — the exact value sub_0801DB94 uses for the
         * player's own floor row. */
        const RoomHeader* hdr = Port_GetRoomHeaderSafe(area, room);
        if (hdr != NULL) {
            uint32_t byHeader = hdr->map_y >> 11;
            found = (int32_t)(byHeader < numFloors ? byHeader : numFloors - 1);
        } else {
            found = 0;
        }
    }
    out->floorCount = (uint8_t)numFloors;
    out->currentFloor = (int8_t)found;
    return 1;
}

int Port_SecondScreenDungeonMap_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                     int32_t floor, const SecondScreenSnapshot* snap, uint32_t tick) {
    const uint8_t dungeonIdx = snap->dungeonIdx;
    const uint8_t area = snap->area;
    const uint8_t room = snap->room;
    const uint8_t* saveFlags = snap->saveFlags;
    const uint8_t dungeonItemBits = snap->dungeonItemBits;
    const DungeonLayout* list;
    const DungeonLayout* lyt;
    const uint16_t* bgPal;
    u32 numColors = 0;
    uint32_t numFloors, f;
    int32_t minX = MAP_PX, minY = MAP_PX, maxX = -1, maxY = -1;
    uint32_t pulseColorIdx;
    int32_t bx0, by0, bw, bh, outW, outH, ox, oy, dx, dy;
    int hasMap = (dungeonItemBits & 1) != 0;     /* bit 0 = dungeon map (HasDungeonMap) */
    int hasCompass = (dungeonItemBits & 2) != 0; /* bit 1 = compass (HasDungeonCompass) */
    struct {
        uint8_t x, y, frame;
    } marks[MAX_MARKERS];
    int32_t numMarks = 0, m;

    if (pixels == NULL || dstW <= 0 || dstH <= 0 || floor < 0) {
        return 0;
    }
    list = GetFloorList(dungeonIdx, (uint32_t)floor);
    if (list == NULL) {
        return 0;
    }
    bgPal = (const uint16_t*)Port_GetRawPaletteGroupBankData(MAP_BG_PALETTE_GROUP, MAP_BG_PALETTE_BANK,
                                                             &numColors);
    if (bgPal == NULL || numColors < 16) {
        return 0; /* ROM data not ready yet — schematic fallback */
    }

    /* Frame on the dungeon-wide room bounding box (every floor, hidden
     * rooms excluded). It's ROM-constant, so the view never shifts as
     * rooms reveal or floors switch — nothing about the save leaks
     * through the framing. */
    numFloors = gDungeonFloorMetadatas[dungeonIdx].numFloors;
    for (f = 0; f < numFloors; f++) {
        for (lyt = gDungeonLayouts[dungeonIdx][f]; lyt != NULL && lyt->area != 0; lyt++) {
            const RoomHeader* hdr;
            int32_t rx, ry, rw, rh;
            if (lyt->unk_2 & 1) {
                continue;
            }
            hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
            if (hdr == NULL) {
                continue;
            }
            rx = (int32_t)(hdr->map_x / 16u);
            ry = (int32_t)((hdr->map_y & 0x7FFu) / 16u);
            rw = (int32_t)(hdr->pixel_width / 16u);
            rh = (int32_t)(hdr->pixel_height / 16u);
            if (rx < minX) {
                minX = rx;
            }
            if (ry < minY) {
                minY = ry;
            }
            if (rx + rw - 1 > maxX) {
                maxX = rx + rw - 1;
            }
            if (ry + rh - 1 > maxY) {
                maxY = ry + rh - 1;
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        return 0;
    }

    memset(sCanvas, 0, sizeof(sCanvas));

    /* PauseMenu_Screen_5 rotates bank 12's colors 8..15 one step every 8
     * GBA ticks (full cycle ~1.07s). tick here runs at the panel's ~20Hz,
     * so x3 approximates the 60Hz cadence. */
    pulseColorIdx = 8u + (((tick * 3u) >> 3) & 7u);

    /* Rooms — DrawDungeonFeatures's reveal logic, verbatim: current room
     * always (pulsing), visited rooms (the save's IsRoomVisited flag) lit,
     * the rest dim and only with the map item; hidden rooms never. */
    for (lyt = list; lyt->area != 0; lyt++) {
        uint32_t features;
        if (lyt->unk_2 & 1) {
            continue;
        }
        if (lyt->area == area && lyt->room == room) {
            features = 8;
        } else if (RoomVisited(lyt, saveFlags)) {
            features = 3;
        } else if (hasMap) {
            features = 2;
        } else {
            continue;
        }
        BlitRoomShape(lyt, features, bgPal, pulseColorIdx);
    }

    /* Markers: DrawDungeonMap's object list, built in the game's order —
     * Link first, then room by room (only rooms with a tile-entity list):
     * with the compass the closed chests and an unbeaten boss, and with or
     * without it the re-entry arrow. Positions are map pixels kept in a
     * byte, as the game's DungeonMapObject keeps them. */
    marks[numMarks].x = (uint8_t)((snap->dungeonMapX >> 4) & 0x7F);
    marks[numMarks].y = (uint8_t)((snap->dungeonMapY >> 4) & 0x7F);
    marks[numMarks].frame = (uint8_t)(Port_SecondScreenDungeonMap_LinkFloor(snap) == floor
                                          ? FRAME_MARKER_PLAYER
                                          : FRAME_MARKER_PLAYER_OTHER_FLOOR);
    numMarks++;
    for (lyt = list; lyt->area != 0 && numMarks < MAX_MARKERS; lyt++) {
        const TileEntity* te = (const TileEntity*)GetRoomProperty(lyt->area, lyt->room, 3);
        const RoomHeader* hdr;
        uint32_t n, bank;
        if (te == NULL) {
            continue;
        }
        hdr = Port_GetRoomHeaderSafe(lyt->area, lyt->room);
        bank = sub_0801DF10(lyt);
        for (n = 0; hasCompass && hdr != NULL && n < 256 && te[n].type != 0 && numMarks < MAX_MARKERS; n++) {
            uint32_t tilePos, cx, cy;
            if (te[n].type != SMALL_CHEST && te[n].type != BIG_CHEST) {
                continue;
            }
            /* Opened chests drop off the map, like DrawDungeonMap's
             * !CheckLocalFlagByBank(bank, localFlag): the flag is the
             * ROM's own (region-native) one, so no remap. */
            if (SaveFlagSet(saveFlags, bank + te[n].localFlag)) {
                continue;
            }
            tilePos = te[n].tilePos;
            if (te[n].type == SMALL_CHEST) {
                cx = ((((tilePos << 4) & 0x3F0u) | 8u) + (hdr->map_x % 0x800u)) >> 4;
                cy = ((((tilePos >> 2) & 0x3F0u) | 8u) + (hdr->map_y % 0x800u)) >> 4;
            } else {
                /* Big chests store pixel coordinates: x in tilePos, y
                 * as the u16 at offset 6 (DrawDungeonMap's
                 * *(u16*)&tileEntity->_6). */
                uint32_t bcy = (uint32_t)te[n]._6 | ((uint32_t)te[n]._7 << 8);
                cx = ((hdr->map_x % 0x800u) + tilePos) >> 4;
                cy = ((hdr->map_y % 0x800u) + bcy) >> 4;
            }
            marks[numMarks].x = (uint8_t)cx;
            marks[numMarks].y = (uint8_t)cy;
            marks[numMarks].frame = FRAME_MARKER_CHEST;
            numMarks++;
        }
        if (hasCompass && hdr != NULL && (lyt->unk_2 & 2) && !SaveFlagSet(saveFlags, FLAG_BANK_0 + dungeonIdx + 1u) &&
            numMarks < MAX_MARKERS) {
            /* Boss room, boss not beaten yet (!CheckGlobalFlag(dungeon_idx
             * + 1)): the boss face at the room's center, which
             * DrawDungeonMapActually skips while sub_080A5F24 says no. */
            marks[numMarks].x = (uint8_t)((((uint32_t)hdr->pixel_width / 2u + hdr->map_x) / 16u) & 0x7Fu);
            marks[numMarks].y = (uint8_t)((((uint32_t)hdr->pixel_height / 2u + hdr->map_y) / 16u) & 0x7Fu);
            marks[numMarks].frame = BossMarksShown(snap) ? FRAME_MARKER_BOSS : 0;
            numMarks++;
        }
        if (lyt->area == snap->dungeonEntryArea && lyt->room == snap->dungeonEntryRoom && numMarks < MAX_MARKERS) {
            marks[numMarks].x = (uint8_t)((snap->dungeonEntryX >> 4) & 0x7F);
            marks[numMarks].y = (uint8_t)((snap->dungeonEntryY >> 4) & 0x7F);
            marks[numMarks].frame = FRAME_MARKER_ENTRY;
            numMarks++;
        }
    }
    /* DrawDirect fills OAM in list order and the lower OAM entry wins, so
     * paint the list backwards: Link ends on top. He does not blink: the
     * map screen draws his frame every frame. */
    for (m = numMarks - 1; m >= 0; m--) {
        if (marks[m].frame != 0) {
            DrawMarkerFrame(marks[m].x, marks[m].y, marks[m].frame, snap->saveLanguage);
        }
    }

    /* Fit + center the framed region into the destination rect. Integer
     * upscale when it fits (the normal case) for the GBA's chunky look;
     * plain nearest-neighbor decimation for pathologically small rects.
     * A 2-map-pixel margin keeps marker overhang inside the frame. */
    bx0 = minX - 2;
    by0 = minY - 2;
    bw = (maxX + 3) - bx0;
    bh = (maxY + 3) - by0;
    if (dstW / bw >= 1 && dstH / bh >= 1) {
        int32_t s = dstW / bw < dstH / bh ? dstW / bw : dstH / bh;
        outW = bw * s;
        outH = bh * s;
    } else if ((int64_t)dstW * bh <= (int64_t)dstH * bw) {
        outW = dstW;
        outH = (int32_t)((int64_t)dstW * bh / bw);
    } else {
        outH = dstH;
        outW = (int32_t)((int64_t)dstH * bw / bh);
    }
    if (outW < 1) {
        outW = 1;
    }
    if (outH < 1) {
        outH = 1;
    }
    ox = dstX + (dstW - outW) / 2;
    oy = dstY + (dstH - outH) / 2;

    for (dy = 0; dy < outH; dy++) {
        int32_t destY = oy + dy;
        int32_t srcY = by0 + (int32_t)((int64_t)dy * bh / outH) + CANVAS_MARGIN;
        uint32_t* row;
        if (destY < 0 || destY >= bufH || srcY < 0 || srcY >= CANVAS_W) {
            continue;
        }
        row = pixels + (size_t)destY * (size_t)stride;
        for (dx = 0; dx < outW; dx++) {
            int32_t destX = ox + dx;
            int32_t srcX = bx0 + (int32_t)((int64_t)dx * bw / outW) + CANVAS_MARGIN;
            uint32_t c;
            if (destX < 0 || destX >= bufW || srcX < 0 || srcX >= CANVAS_W) {
                continue;
            }
            c = sCanvas[(size_t)srcY * CANVAS_W + (size_t)srcX];
            if (c != 0) {
                row[destX] = c;
            }
        }
    }
    return 1;
}
