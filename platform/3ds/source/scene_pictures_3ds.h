#pragma once
/* Pictures that extend a scene the GBA drew at 240x160, for the renderers (docs/wide-scene-extension.md).
 * The title screen's forest, 400x240: the player's own title-forest.bin in the data folder, else the
 * one shipped in the romfs. Loaded when first asked for, freed by ScenePictures_Release. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SCENE_TITLE_W = 400, SCENE_TITLE_H = 240, SCENE_CAVE_W = 400, SCENE_CAVE_H = 240, SCENE_CAVE_FIRST_PALETTE = 11 };
/* The Crenel paths' shaft (area 0x12 room 00, 240x800): the generated strips beside the room, 80 columns each side,
 * for its three layers. The ground (BG2) has the room's top (32 rows) and one turn of the 320 rows it repeats, which
 * the renderer repeats too; the rock ledges (BG1) and their shadows (BG3) the 960 rows their scroll shows. */
enum { SCENE_SHAFT_STRIP_W = 80, SCENE_SHAFT_ROOM_W = 240, SCENE_SHAFT_GROUND_H = 352, SCENE_SHAFT_LOOP_START = 32,
       SCENE_SHAFT_LOOP_LENGTH = 320, SCENE_SHAFT_LAYER_H = 960 };
typedef enum { SCENE_SHAFT_GROUND, SCENE_SHAFT_LEDGES, SCENE_SHAFT_SHADOWS, SCENE_SHAFT_LAYERS } SceneShaftLayer;

bool ScenePictures_TitleAvailable(void);
/* GBA 15-bit colours, row-major, 0 where the game's own picture shows (the shipped files keep
 * only the generated parts); NULL when there is no picture. */
const uint16_t* ScenePictures_Title555(void);
/* The same through the game's colour conversion (colour correction as it is now), 0 kept. */
const uint32_t* ScenePictures_TitleAbgr(void);
/* For the software renderer: faded as the shown frame's palettes are (the GPU renderer fades
 * the whole frame itself), else ScenePictures_TitleAbgr. */
const uint32_t* ScenePictures_TitleAbgrFaded(void);
void ScenePictures_Release(void);

/* The pause menu's background, 400x240 (pause-background.bin, tools/scene-extend/to_console.py pause): made by code
 * from the game's own pattern, so shown in plain WIDE too. GBA 15-bit colours, 0 where the game's own picture is;
 * NULL when there is none. Freed by ScenePictures_ReleasePause. */
enum { SCENE_PAUSE_W = 400, SCENE_PAUSE_H = 240 };
bool ScenePictures_PauseAvailable(void);
const uint16_t* ScenePictures_Pause555(void);
void ScenePictures_ReleasePause(void);

/* The file select's cave, 400x240 (fileselect-cave.bin, tools/scene-extend/to_console.py fileselect):
 * one byte a pixel, (palette - SCENE_CAVE_FIRST_PALETTE) * 16 + index into BG palettes 11-14, which
 * the game cycles; 0 where the game's own picture is. NULL when there is none. Freed by
 * ScenePictures_ReleaseCave. */
bool ScenePictures_CaveAvailable(void);
const uint8_t* ScenePictures_CaveIndices(void);
void ScenePictures_ReleaseCave(void);

/* A layer's strips (crenel-paths-{ground,ledges,shadows}.bin, tools/scene-extend/to_console.py crenel-*): GBA
 * 15-bit colours, 2 * SCENE_SHAFT_STRIP_W wide (left strip, then right), 0 where empty; NULL when there is none.
 * `height` gets its rows. Freed by ScenePictures_ReleaseShaft. */
const uint16_t* ScenePictures_Shaft555(SceneShaftLayer layer, unsigned* height);
void ScenePictures_ReleaseShaft(void);
/* Where the shaft lies on the view, set by the game each frame (port_linked_stubs.c): `groundBg` the BG that draws its
 * ground, -1 when the shaft is not shown in Full View; x the left strip's left edge on the view, y[layer] its top. */
void ScenePictures_SetShaftView(int groundBg, int x, int groundY, int ledgesY, int shadowsY);
int ScenePictures_ShaftView(int* x, int y[SCENE_SHAFT_LAYERS]);

/* Reads a scene file into `out` (exactly `bytes`): the player's own `sd` path, else the shipped `romfs` one. The file
 * is the picture as it is (`bytes` long, as older to_console.py wrote it), a zlib stream (tools/scene-extend/
 * to_console.py's default) or the GBA BIOS's LZ77 (0x10 and the size; to_console.py --lz77, the fallback should zlib
 * ever be missing). False when neither file is there or it is none of those. */
bool ScenePictures_ReadFile(const char* sd, const char* romfs, void* out, size_t bytes);
