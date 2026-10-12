#pragma once
/* A screen fade the GPU renderer can apply itself; see fade_gpu_3ds.c. */
#include <stdbool.h>
#include <stdint.h>

/* palRamBg / palRamObj are the palettes the renderer would otherwise read.
 * True when they are exactly gPaletteBuffer faded by one (intensity, color)
 * for all 32 palettes. Then *bgPalette / *objPalette are the unfaded palettes
 * to decode from and the picture is blended toward grey *level by *weight/255. */
bool FadeGpu3DS_State(const uint16_t* palRamBg, const uint16_t* palRamObj, const uint16_t** bgPalette,
                      const uint16_t** objPalette, uint8_t* weight, uint8_t* level);

/* Once per logic frame, before it is presented (Platform3DS_BeginFrameBoundary):
 * remembers the palette buffer and fade parameters the game's vblank will turn
 * into PAL_RAM, which is what the next presentation shows. */
void FadeGpu3DS_OnFrameBoundary(void);
