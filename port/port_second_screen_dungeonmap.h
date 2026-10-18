#ifndef PORT_SECOND_SCREEN_DUNGEONMAP_H
#define PORT_SECOND_SCREEN_DUNGEONMAP_H

/*
 * Authentic dungeon-map rendering for the second screen: the same room
 * shapes, floor structure and reveal rules the game's own map screen
 * (src/subtask/subtaskLocalMapHint.c) shows, decoded at runtime from
 * ROM/asset data — no baked pixels, same policy as the item icons.
 *
 * Reveal rules mirror the real map so the second screen never cheats:
 * the dungeon map item gates the layout, the compass gates markers,
 * visited rooms and opened chests come from the save's flags.
 *
 * Same threading contract as port_second_screen_worldmap.h: ROM/static
 * decomp tables only; all live state (dungeon items, flags, player
 * position, language) arrives in the snapshot.
 */

#include "port_second_screen_state.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t floorCount;  /* 0 = no authentic map data for this dungeon */
    int8_t currentFloor; /* index into the drawable floors (display order,
                          * topmost floor = 0) of the floor `room` is on */
} SecondScreenDungeonMapInfo;

/* Fills `out` for the given dungeon/area/room. Returns 1 when authentic
 * map data exists, 0 otherwise (out->floorCount is zeroed then). */
int Port_SecondScreenDungeonMap_GetInfo(uint8_t dungeonIdx, uint8_t area, uint8_t room,
                                        SecondScreenDungeonMapInfo* out);

/* Draws floor `floor` (display row, top = 0) of snap->dungeonIdx into
 * `pixels` (RGBA8888, stride-pixels-per-row, bufW x bufH), fitted and
 * centered inside the destination rect, the way the pause map draws it
 * (DrawDungeonFeatures + DrawDungeonMap, src/common.c): rooms by the map
 * item and the save's visited flags, Link (his own frame on his floor,
 * another on the rest), with the compass the closed chests and an unbeaten
 * boss, and the re-entry arrow. `tick` drives the current room's pulse.
 * Returns 1 if authentic art was drawn, 0 for callers to fall back to the
 * schematic renderer. */
int Port_SecondScreenDungeonMap_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                     int32_t floor, const SecondScreenSnapshot* snap, uint32_t tick);

/* Frames of the map screen's DrawDirect sheet drawn beside the floor
 * list by DrawDungeonMapActually (src/menu/pauseMenu.c). Each pair
 * alternates: the cursor on the floor being viewed by bit 4 of the game's
 * frame count, Link on his floor and the boss on its floor by bit 5. */
#define SS_DMAP_FRAME_FLOOR_CURSOR_A 0x75u /* (ticks & 0x10) == 0 */
#define SS_DMAP_FRAME_FLOOR_CURSOR_B 0x74u
#define SS_DMAP_FRAME_LINK_FLOOR_A 0x78u /* (ticks & 0x20) != 0 */
#define SS_DMAP_FRAME_LINK_FLOOR_B 0x79u
#define SS_DMAP_FRAME_BOSS_FLOOR_A 0x7au /* (ticks & 0x20) != 0 */
#define SS_DMAP_FRAME_BOSS_FLOOR_B 0x7bu

/* Draws one frame of that sheet anchored at (x, y) as the game anchors
 * gOamCmd, each source pixel a scale x scale block, with the OBJ tiles the
 * map screen holds for the save's language (snap->saveLanguage). Returns 1
 * when it drew any pixel, 0 when the frame or its tiles are not available. */
int Port_SecondScreenDungeonMap_DrawFrame(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride, int32_t x,
                                          int32_t y, int32_t scale, uint32_t frameIndex, uint8_t saveLanguage);

/* Pixel bounds of that frame around its anchor at scale 1 ([x0, x1) x
 * [y0, y1)). Returns 0 when the frame is not available. */
int Port_SecondScreenDungeonMap_FrameBounds(uint32_t frameIndex, int32_t* x0, int32_t* y0, int32_t* x1, int32_t* y1);

/* The floor name frame of display row `row` (the game's own "1F", "B1",
 * "PB", "S.1", ... sprites: highestFloor + 0x82 - row), -1 if none. */
int Port_SecondScreenDungeonMap_FloorLabelFrame(uint8_t dungeonIdx, int32_t row);

/* Display row of Link's floor (sub_0801DB94: dungeon_map_y >> 11). */
int Port_SecondScreenDungeonMap_LinkFloor(const SecondScreenSnapshot* snap);

/* Display row the boss icon marks (highestFloor - unk_2), or -1 when the
 * pause map shows none: no compass, or sub_080A5F24 says no. */
int Port_SecondScreenDungeonMap_BossFloor(const SecondScreenSnapshot* snap);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_DUNGEONMAP_H */
