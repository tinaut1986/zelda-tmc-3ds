#ifndef GBA_LOOK_3DS_H
#define GBA_LOOK_3DS_H

/*
 * The "GBA look" on the top screen, as in ../mzm (port_gba_screen_fx.c, port_gba_bezel.c):
 *
 *   - GBA GRID: dark lines on every other row of the game's own pixels (HIGH adds the columns),
 *     lined up with those pixels at any scale. OFF / LOW / MED / HIGH (gba_grid).
 *   - GBA FRAME: the console's body drawn around a picture that leaves the screen's border
 *     empty: the 240x160 picture of Pixel Perfect (the whole frame) and the 360x240 one of
 *     Scaled (the two side strips). Full View (400x240) has no border to fill (gba_frame).
 *
 * Both are drawn over the finished picture, once per eye, in the presenter's citro2d scene
 * (platform_gpu_3ds.c). The grid is a single alpha-tested quad from an 8-bit mask baked only
 * when something it depends on changes; the frame is a 512x256 picture decompressed on first
 * use, drawn as the pieces around the picture.
 *
 * This file talks to citro2d/citro3d and must NOT pull in any game header (their u32 clashes
 * with libctru's); settings come in through the plain accessors of port_config_3ds.c.
 */

#include <stdbool.h>

/* Draws what is enabled for a picture shown at (drawX, drawY, drawW x drawH) on the 400x240 top
 * screen that was made from srcW x srcH game pixels. Flushes the frame's quads first and leaves
 * the texture environment, blending and alpha test for the caller to put back; returns false
 * (having changed nothing) when there was nothing to draw. */
bool GbaLook3DS_Draw(int drawX, int drawY, int drawW, int drawH, int srcW, int srcH);

void GbaLook3DS_Shutdown(void);

#endif /* GBA_LOOK_3DS_H */
