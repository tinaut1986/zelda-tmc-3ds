#pragma once
/* P4.9: the neighbouring room drawn beyond the room's edge in a wide view. Where the view reaches past
 * the room's rectangle (a room narrower or shorter than 400x240, centred with the rest black), the cells
 * come from the room that borders it in the area's room grid (RoomResInfo.map_x/map_y/pixel_*), not
 * from nothing. Only the two tile layers: no entities, no collision. A neighbour can be drawn only if
 * it uses the same tile set and metatile definitions as the current room, since those are what VRAM and
 * the layers hold. */
#include <stdbool.h>
#include <stdint.h>

/* Once a tick (Port_Widescreen_UpdateShadows): follows the room, rebuilds the window when it changes. */
void Port_NeighbourRooms_Update(void);
/* The 8x8 entry of a layer (topLayer 0 = the bottom map, 1 = the top one) at the current room's tile
 * (column, row from its origin) when that lies outside the room: the neighbour's, or `blank`. */
uint16_t Port_NeighbourRooms_Tile(int topLayer, int column, int row, uint16_t blank);
/* For the probe's log and the debug overlay: what the last build found. */
typedef struct PortNeighbourStats {
    unsigned neighbours;     /* bordering rooms drawn */
    unsigned otherTileset;   /* bordering rooms left out: another tile set */
    unsigned unreadable;     /* bordering rooms left out: their map could not be read */
    unsigned cells;          /* cells filled from neighbours */
} PortNeighbourStats;
PortNeighbourStats Port_NeighbourRooms_Stats(void);
