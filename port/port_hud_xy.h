#ifndef PORT_HUD_XY_H
#define PORT_HUD_XY_H

/* The X and Y item buttons in the top screen's HUD (PLAN P2.6, 3DS only).
 *
 * src/ui.c draws them as the game draws A and B: the A bubble (sprite 505,
 * frame 0) and the item's icon (sprite 322) through DrawDirect, so both
 * renderers, the palette fades and the Wide / Full View anchor treat them like
 * the game's own HUD. OBJ VRAM has no room for their tiles, so the OAM entries
 * point at tiles the frame does not use (PORT_HUD_XY_TILE_*) and the platform
 * writes the tiles there only while it renders the frame
 * (platform/3ds/source/hud_xy_3ds.c), putting VRAM back afterwards: the game
 * never sees them. */

#include "gba/types.h"

#define PORT_HUD_XY_SLOTS 2u
/* Four tiles of bubble, then up to eight of icon, per slot (X = 0, Y = 1). */
#define PORT_HUD_XY_TILE_FIRST 0x3E0u
#define PORT_HUD_XY_TILE_COUNT 24u
#define PORT_HUD_XY_BUBBLE_TILE(slot) (PORT_HUD_XY_TILE_FIRST + 4u * (slot))
#define PORT_HUD_XY_ICON_TILE(slot) (PORT_HUD_XY_TILE_FIRST + 8u + 8u * (slot))
#define PORT_HUD_XY_ICON_MAX_TILES 8u

/* Game thread, while the frame's sprites are drawn. */
void Port_HudXY_BeginFrame(void);
/* OAM entries [first, end) of gOAMControls.oam hold `slot`'s bubble. */
void Port_HudXY_AddBubble(unsigned slot, unsigned first, unsigned end);
/* ... or its icon: `numTiles` tiles at `tiles`, and the ammo count over its
 * first two tiles when `count` >= 0 (as the HUD does for A and B). */
void Port_HudXY_AddIcon(unsigned slot, unsigned first, unsigned end, const void* tiles, unsigned numTiles,
                        int count);
/* gOAMControls.oam has just been copied to OAM (UpdateDisplayControls). */
void Port_HudXY_CommitOam(void);

/* Render thread (the main thread, around the top screen's render). */
void Port_HudXY_BeginRender(void);
void Port_HudXY_EndRender(void);

#endif
