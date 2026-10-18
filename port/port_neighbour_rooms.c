#include "port_neighbour_rooms.h"

#include "area.h"
#include "global.h"
#include "main.h"
#include "map.h"
#include "room.h"
#include "port_rom.h"

#include <string.h>

extern void DebugLog_Printf(const char* fmt, ...);

/* The window of tiles around the current room, in tiles from its origin: a margin each side (a 400x240
 * view centres a small room, so the black beside it is at most 25 tiles wide, 15 tall) and the room's
 * own size up to WIN_ROOM. A room larger than that in an axis never shows black in it. */
enum { MARGIN_X = 32, MARGIN_Y = 20, WIN_COLS = 128 + 2 * MARGIN_X, WIN_ROWS = 96 + 2 * MARGIN_Y };
enum { NO_TILE = 0xFFFF, MAP_STRIDE = 64 };

static uint16_t sWin[2][WIN_ROWS][WIN_COLS];
static uint16_t sMeta[2][MAP_STRIDE * MAP_STRIDE];
static u32 sKey = 0xffffffffu;
static unsigned sTicks;
static int sActive;
static PortNeighbourStats sStats;

static u32 RoomKey(void) {
    return ((u32)gRoomControls.area << 8) | gRoomControls.room;
}

/* The two layer maps of a room's map definition chain, decompressed: sMeta[0] bottom, sMeta[1] top. */
static bool ReadMeta(const RoomResInfo* info) {
    const MapDataDefinition* def = info->map;
    if (def == NULL) return false;
    const bool packed = gRomData != NULL && (const u8*)def >= gRomData && (const u8*)def < gRomData + gRomSize;
    const size_t step = packed ? 12 : sizeof(MapDataDefinition);
    bool got[2] = { false, false };
    for (int i = 0; i < 16; ++i) {
        MapDataDefinition entry;
        Port_DecodeMapDataDefinition((const u8*)def + i * step, &entry);
        if (entry.dest != NULL) {
            const int layer = entry.dest == (void*)gMapBottom.mapData ? 0 : entry.dest == (void*)gMapTop.mapData ? 1 : -1;
            if (layer >= 0 && (entry.size & MAP_COMPRESSED) != 0) {
                const u8* src = (entry.src & MAP_SRC_FILE) != 0 ? NULL : gMapData + (entry.src & 0x7fffffff);
                if (src == NULL) return false;
                LZ77UnCompWram(src, sMeta[layer]);
                got[layer] = true;
            } else if (layer >= 0) {
                return false; /* stored uncompressed: not seen in the area maps, left to a later look */
            }
        }
        if ((entry.src & MAP_MULTIPLE) == 0) break;
    }
    return got[0] && got[1];
}

static void Build(void) {
    memset(sWin, 0xFF, sizeof(sWin));
    memset(&sStats, 0, sizeof(sStats));
    const RoomResInfo* cur = &gArea.roomResInfos[gRoomControls.room];
    const int curW = gRoomControls.width / 8, curH = gRoomControls.height / 8;
    const int ox = gRoomControls.origin_x, oy = gRoomControls.origin_y;
    /* The window's rectangle in world pixels. */
    const int wx0 = ox - MARGIN_X * 8, wy0 = oy - MARGIN_Y * 8;
    const int wx1 = wx0 + WIN_COLS * 8, wy1 = wy0 + WIN_ROWS * 8;
    for (unsigned j = 0; j < MAX_ROOMS; ++j) {
        const RoomResInfo* nb = &gArea.roomResInfos[j];
        if (j == gRoomControls.room || nb->pixel_width == 0 || nb->pixel_height == 0) continue;
        if ((int)nb->map_x >= wx1 || (int)nb->map_x + nb->pixel_width <= wx0 || (int)nb->map_y >= wy1 ||
            (int)nb->map_y + nb->pixel_height <= wy0)
            continue;
        /* Only rooms that touch the current one (or sit around it): the same tile set and definitions. */
        if (nb->tileSet != cur->tileSet || nb->tiles != cur->tiles) {
            ++sStats.otherTileset;
            continue;
        }
        /* A first entry of 0xFFFF marks a room drawn as a picture, not a map (LoadRoomMapData's clearBottomMap). */
        const unsigned metaCols = nb->pixel_width / 16u;
        if (metaCols == 0 || metaCols > MAP_STRIDE || nb->pixel_height / 16u > MAP_STRIDE || !ReadMeta(nb) ||
            sMeta[0][0] == 0xFFFF) {
            ++sStats.unreadable;
            continue;
        }
        ++sStats.neighbours;
        MapLayer* const layers[2] = { &gMapBottom, &gMapTop };
        for (int row = -MARGIN_Y; row < WIN_ROWS - MARGIN_Y; ++row) {
            const int py = oy + row * 8 - (int)nb->map_y;
            if (py < 0 || py >= nb->pixel_height) continue;
            for (int col = -MARGIN_X; col < WIN_COLS - MARGIN_X; ++col) {
                if (col >= 0 && col < curW && row >= 0 && row < curH) continue; /* the room itself */
                const int px = ox + col * 8 - (int)nb->map_x;
                if (px < 0 || px >= nb->pixel_width) continue;
                for (int layer = 0; layer < 2; ++layer) {
                    uint16_t* cell = &sWin[layer][row + MARGIN_Y][col + MARGIN_X];
                    if (*cell != NO_TILE) continue; /* a nearer neighbour got it */
                    /* As stored: rows of the room's own width (the game spreads them to MAP_STRIDE after
                     * loading, sub_0807C8B0). */
                    const unsigned meta = sMeta[layer][(py >> 4) * metaCols + (px >> 4)];
                    /* Special tiles (>= 0x4000: moved or removed by the game) keep their plain index. */
                    const unsigned index = meta & 0x3FFF;
                    *cell = layers[layer]->subTiles[index * 4 + ((py >> 3) & 1) * 2 + ((px >> 3) & 1)];
                    ++sStats.cells;
                }
            }
        }
    }
    DebugLog_Printf("neighbours: area %u room %u (%dx%d tiles): %u drawn, %u other tileset, %u unreadable, %u cells",
                    gRoomControls.area, gRoomControls.room, curW, curH, sStats.neighbours, sStats.otherTileset,
                    sStats.unreadable, sStats.cells);
}

void Port_NeighbourRooms_Update(void) {
    const u32 key = RoomKey();
    if (key != sKey) {
        sKey = key;
        sTicks = 0;
        sActive = 0;
        return;
    }
    ++sTicks;
    /* The layers' tile definitions are in place a few ticks after the room starts; once again later in case. */
    if (sTicks == 8 || sTicks == 120) {
        Build();
        sActive = 1;
    }
}

uint16_t Port_NeighbourRooms_Tile(int topLayer, int column, int row, uint16_t blank) {
    if (!sActive) return blank;
    const int c = column + MARGIN_X, r = row + MARGIN_Y;
    if (c < 0 || c >= WIN_COLS || r < 0 || r >= WIN_ROWS) return blank;
    const uint16_t entry = sWin[topLayer ? 1 : 0][r][c];
    return entry == NO_TILE ? blank : entry;
}

PortNeighbourStats Port_NeighbourRooms_Stats(void) {
    return sStats;
}
