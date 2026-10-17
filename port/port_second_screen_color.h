#pragma once
/* The bottom screen's game art (the world map, the dungeon maps, the item icons, the collection) decodes the
 * game's own RGB555 palettes; with COLOURS on it goes through the same LCD curve as the top screen's picture
 * (mode1.c, port_ppu_gpu_3ds_model.c), so the two look alike. The UI theme's chrome is not game art and
 * keeps its own colours. */
#include <stdint.h>

/* A GBA colour as RGBA8888 (A 0xff, then B, G, R from the top byte down, as the second screen's surfaces). */
uint32_t Port_SecondScreen_Rgb555(uint16_t c);
/* A number that changes whenever the COLOURS option does: art cached under another value is remade. */
unsigned Port_SecondScreen_ColorEpoch(void);
