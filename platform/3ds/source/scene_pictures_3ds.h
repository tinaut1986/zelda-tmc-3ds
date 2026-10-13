#pragma once
/* Pictures that extend a scene the GBA drew at 240x160, for the renderers (docs/wide-scene-extension.md).
 * The title screen's forest, 400x240: the player's own title-forest.bin in the data folder, else the
 * one shipped in the romfs. Loaded when first asked for, freed by ScenePictures_Release. */
#include <stdbool.h>
#include <stdint.h>

enum { SCENE_TITLE_W = 400, SCENE_TITLE_H = 240 };

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
