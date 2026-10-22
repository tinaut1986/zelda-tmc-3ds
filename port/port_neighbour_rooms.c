#include "port_neighbour_rooms.h"

#include "area.h"
#include "entity.h"
#include "game.h"
#include "global.h"
#include "main.h"
#include "map.h"
#include "object.h"
#include "room.h"
#include "subtask.h"
#include "tileMap.h"
#include "port_generic_entity.h"
#include "port_rom.h"
#include "port_runtime_config.h"

#include <string.h>

extern void DebugLog_Printf(const char* fmt, ...);

/* The window of tiles around the current room, in tiles from its origin: the room (up to 128x96 tiles) and
 * a whole 400x240 view each side, since a border scroll starts with the view still in the outgoing room
 * (it runs with the incoming room already current) and the cells beside that one need their rooms too. */
enum { MARGIN_X = 52, MARGIN_Y = 32, WIN_COLS = 128 + 2 * MARGIN_X, WIN_ROWS = 96 + 2 * MARGIN_Y };
enum { NO_TILE = 0xFFFF, MAP_STRIDE = 64 };

static uint16_t sWin[2][WIN_ROWS][WIN_COLS];
static uint16_t sMeta[2][MAP_STRIDE * MAP_STRIDE];
/* A picture room's tilemaps (see ReadMeta): up to four 32x32 blocks per layer, as stored. */
static uint16_t sPicture[2][MAP_STRIDE * MAP_STRIDE];
static u32 sKey = 0xffffffffu;
static unsigned sTicks;
static int sActive;
static PortNeighbourStats sStats;
static u64 sDrawn; /* the rooms the last build drew */
static int sMode = -1; /* the setting the module last ran with: 0 OFF, 1 ON */
static int sCurtainClosed; /* a Gentari curtain of the current room is shut (see CurtainClosed) */

static u32 RoomKey(void) {
    return ((u32)gRoomControls.area << 8) | gRoomControls.room;
}

enum { META_NONE, META_TILES, META_PICTURE };

/* The two layer maps of a room's map definition chain, decompressed: sMeta[0] bottom, sMeta[1] top. A room drawn
 * as a picture (the Minish houses: a bottom map of 0xFFFF and no top) has no metatile map; its picture is the
 * tilemap of its bottom_special entry, as stored (LoadRoomGfx spreads it with sub_0807C5F4), into sPicture. */
static int ReadMeta(const RoomResInfo* info) {
    const MapDataDefinition* def = info->map;
    if (def == NULL) return META_NONE;
    const bool packed = gRomData != NULL && (const u8*)def >= gRomData && (const u8*)def < gRomData + gRomSize;
    const size_t step = packed ? 12 : sizeof(MapDataDefinition);
    bool got[2] = { false, false }, picture[2] = { false, false };
    for (int i = 0; i < 16; ++i) {
        MapDataDefinition entry;
        Port_DecodeMapDataDefinition((const u8*)def + i * step, &entry);
        if (entry.dest != NULL) {
            const int layer = entry.dest == (void*)gMapBottom.mapData ? 0 : entry.dest == (void*)gMapTop.mapData ? 1 : -1;
            const int special = entry.dest == (void*)gMapDataBottomSpecial ? 0 : entry.dest == (void*)gMapDataTopSpecial ? 1 : -1;
            if ((layer >= 0 || special >= 0) && (entry.size & MAP_COMPRESSED) != 0) {
                const u8* src = (entry.src & MAP_SRC_FILE) != 0 ? NULL : gMapData + (entry.src & 0x7fffffff);
                if (src == NULL) return META_NONE;
                if (layer >= 0) {
                    LZ77UnCompWram(src, sMeta[layer]);
                    got[layer] = true;
                } else {
                    LZ77UnCompWram(src, sPicture[special]);
                    picture[special] = true;
                }
            } else if (layer >= 0) {
                return META_NONE; /* stored uncompressed: not seen in the area maps, left to a later look */
            }
        }
        if ((entry.src & MAP_MULTIPLE) == 0) break;
    }
    /* A first entry of 0xFFFF marks a room drawn as a picture, not a map (LoadRoomGfx's clearBottomMap). */
    if (got[0] && sMeta[0][0] == 0xFFFF) return picture[0] ? META_PICTURE : META_NONE;
    return got[0] && got[1] ? META_TILES : META_NONE;
}

/* A picture room's cell at (cx, cy) tiles from its origin: sub_0807C5F4's blocks, one 32x32 block for the
 * first 32 columns and rows, then the one for the next 32 columns, the next 32 rows, the corner. */
static uint16_t PictureCell(const uint16_t* pic, const RoomResInfo* nb, int cx, int cy) {
    const bool wide = nb->pixel_width > 0xff, tall = nb->pixel_height > 0xff;
    const int bx = cx >> 5, by = cy >> 5;
    if (bx > 1 || by > 1 || (bx && !wide) || (by && !tall)) return NO_TILE;
    const int block = by == 0 ? bx : bx == 0 ? (wide ? 2 : 1) : 3;
    return pic[block * 32 * 32 + (cy & 31) * 32 + (cx & 31)];
}

static bool SharesEdge(const RoomResInfo* a, const RoomResInfo* b) {
    const int ax1 = a->map_x + a->pixel_width, ay1 = a->map_y + a->pixel_height;
    const int bx1 = b->map_x + b->pixel_width, by1 = b->map_y + b->pixel_height;
    const bool overlapY = a->map_y < by1 && b->map_y < ay1, overlapX = a->map_x < bx1 && b->map_x < ax1;
    return ((ax1 == b->map_x || bx1 == a->map_x) && overlapY) || ((ay1 == b->map_y || by1 == a->map_y) && overlapX);
}

/* The rooms reachable from `room` through shared edges. An area also packs unrelated rooms side by side with a
 * gap between them (the houses of HOUSE_INTERIORS_*), which must not show; a dungeon's or a town's rooms abut. */
static u32 sConnectedKey = 0xffffffffu;

static u64 Connected(unsigned room) {
    static u64 mask;
    const u32 k = ((u32)gRoomControls.area << 8) | room;
    if (k == sConnectedKey) return mask;
    sConnectedKey = k;
    mask = (room < MAX_ROOMS) ? (u64)1 << room : 0;
    for (bool grew = mask != 0; grew;) {
        grew = false;
        for (unsigned j = 0; j < MAX_ROOMS; ++j) {
            const RoomResInfo* nb = &gArea.roomResInfos[j];
            if ((mask >> j) & 1 || nb->pixel_width == 0 || nb->pixel_height == 0) continue;
            for (unsigned i = 0; i < MAX_ROOMS; ++i) {
                if ((mask >> i) & 1 && SharesEdge(&gArea.roomResInfos[i], nb)) {
                    mask |= (u64)1 << j;
                    grew = true;
                    break;
                }
            }
        }
    }
    return mask;
}

/* Gentari's curtains hide the next room until they are drawn open: while a GENTARI_CURTAIN of the current room is
 * still shut (action 1) or opening (action 2; 3 is open), the neighbours stay out of the picture, so the room behind
 * the curtain is not seen through the wall around it. */
static int CurtainClosed(void) {
    for (LinkedList* list = gEntityLists; list < &gEntityLists[9]; ++list) {
        if (list->first == NULL) continue;
        for (Entity* e = list->first; e != (Entity*)list && e != NULL; e = e->next) {
            if (e->kind == OBJECT && e->id == GENTARI_CURTAIN && (e->action == 1 || e->action == 2)) return 1;
        }
    }
    return 0;
}

static void Build(void) {
    memset(sWin, 0xFF, sizeof(sWin));
    memset(&sStats, 0, sizeof(sStats));
    sDrawn = 0;
    /* The first build of a room runs while the area is still loading (0x0 tiles in the log): the room table can
     * be the previous area's, and a mask worked out from it would be kept for the whole room. */
    sConnectedKey = 0xffffffffu;
    const RoomResInfo* cur = &gArea.roomResInfos[gRoomControls.room];
    const int curW = gRoomControls.width / 8, curH = gRoomControls.height / 8;
    const int ox = gRoomControls.origin_x, oy = gRoomControls.origin_y;
    /* The window's rectangle in world pixels. */
    const int wx0 = ox - MARGIN_X * 8, wy0 = oy - MARGIN_Y * 8;
    const int wx1 = wx0 + WIN_COLS * 8, wy1 = wy0 + WIN_ROWS * 8;
    const u64 connected = sCurtainClosed ? 0 : Connected(gRoomControls.room);
    for (unsigned j = 0; j < MAX_ROOMS; ++j) {
        const RoomResInfo* nb = &gArea.roomResInfos[j];
        if (j == gRoomControls.room || nb->pixel_width == 0 || nb->pixel_height == 0) continue;
        if (((connected >> j) & 1) == 0) {
            ++sStats.apart;
            continue;
        }
        if ((int)nb->map_x >= wx1 || (int)nb->map_x + nb->pixel_width <= wx0 || (int)nb->map_y >= wy1 ||
            (int)nb->map_y + nb->pixel_height <= wy0)
            continue;
        /* Only rooms that touch the current one (or sit around it): the same tile set and definitions. */
        if (nb->tileSet != cur->tileSet || nb->tiles != cur->tiles) {
            ++sStats.otherTileset;
            continue;
        }
        const unsigned metaCols = nb->pixel_width / 16u;
        const int kind = (metaCols == 0 || metaCols > MAP_STRIDE || nb->pixel_height / 16u > MAP_STRIDE) ? META_NONE
                                                                                                         : ReadMeta(nb);
        if (kind == META_NONE) {
            ++sStats.unreadable;
            continue;
        }
        ++sStats.neighbours;
        sDrawn |= (u64)1 << j;
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
                    if (kind == META_PICTURE) {
                        *cell = PictureCell(sPicture[layer], nb, px >> 3, py >> 3);
                        if (*cell != NO_TILE) ++sStats.cells;
                        continue;
                    }
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
    DebugLog_Printf("neighbours: area %u room %u (%dx%d tiles): %u drawn, %u apart, %u other tileset, %u unreadable, %u cells",
                    gRoomControls.area, gRoomControls.room, curW, curH, sStats.neighbours, sStats.apart, sStats.otherTileset,
                    sStats.unreadable, sStats.cells);
}

/* The neighbours' ARCHWAY objects. A door's frame is a sprite over placeholder tiles (drawn as "カベ", wall), so a
 * neighbour drawn from its map alone shows those. Inert copies of its archways are spawned in the current room,
 * marked in their EntityData's type2 (bit 31; the archway reads only its low byte). Every other object of the
 * neighbours is left out (issue #38).
 *
 * The copies are compared with the ones the drawn neighbours call for, by world position and frame, and replaced
 * only when they differ: a save state brings its own (they are entities), and so does the room an AuxCutscene
 * returns to, whose entity lists the subtask set aside and restored. Deleting those on a room key change showed
 * the placeholders until the new copies drew, and after the cutscene it wrapped the entity count: the subtask
 * leaves gEntCount at 0 with the restored room's entities alive (the GBA's own bookkeeping), each deletion
 * subtracts one more, and at 253 the game finds no free slot for anything. */
enum { ARCHWAY_MARK = 0x80000000u, MAX_ARCHWAYS = 16 };
extern const void* Port_ResolveRegionData(const void* data);

typedef struct {
    s16 x, y;
    u8 frame;
} ArchwayAt;

static bool IsArchwayCopy(Entity* e) {
    return e->kind == OBJECT && e->id == ARCHWAY && (GE_FIELD(e, field_0x7c)->WORD_U & ARCHWAY_MARK) != 0;
}

/* The copies alive in the entity lists, into `at` (up to MAX_ARCHWAYS), or deleted. */
static unsigned ScanCopies(ArchwayAt* at, bool remove) {
    unsigned n = 0;
    for (LinkedList* list = gEntityLists; list < &gEntityLists[9]; ++list) {
        if (list->first == NULL) continue;
        for (Entity *e = list->first, *next; e != (Entity*)list && e != NULL; e = next) {
            next = e->next;
            if (!IsArchwayCopy(e)) continue;
            if (at != NULL && n < MAX_ARCHWAYS) {
                at[n].x = e->x.HALF.HI;
                at[n].y = e->y.HALF.HI;
                at[n].frame = e->type2;
            }
            ++n;
            if (remove) DeleteEntity(e);
        }
    }
    return n;
}

/* The copies the drawn neighbours call for, as EntityData of the current room. */
static unsigned WantedCopies(EntityData* out) {
    unsigned n = 0;
    if (Port_Config_GetNeighbourRooms() == 0) return 0;
    const int ox = gRoomControls.origin_x, oy = gRoomControls.origin_y;
    for (unsigned j = 0; j < MAX_ROOMS && n < MAX_ARCHWAYS; ++j) {
        if (((sDrawn >> j) & 1) == 0) continue;
        const RoomResInfo* nb = &gArea.roomResInfos[j];
        for (u32 prop = 0; prop < 2; ++prop) {
            const EntityData* dat =
                (const EntityData*)Port_ResolveRegionData(GetRoomProperty(gRoomControls.area, j, prop));
            for (; dat != NULL && dat->kind != 0xFF && n < MAX_ARCHWAYS; ++dat) {
                if ((dat->kind & 0xF) != OBJECT || dat->id != ARCHWAY || (dat->flags & 0xF0) != 0) continue;
                out[n] = *dat;
                out[n].type2 |= ARCHWAY_MARK;
                out[n].xPos = (u16)(nb->map_x + dat->xPos - ox);
                out[n].yPos = (u16)(nb->map_y + dat->yPos - oy);
                ++n;
            }
        }
    }
    return n;
}

static void UpdateArchways(void) {
    /* In plain play (or AuxCutscene 5, the wide one in a house: Zelda leaving Link's house) with no room change
     * under way. */
    const bool play = gMain.substate == GAMEMAIN_UPDATE ||
                      (gMain.substate == GAMEMAIN_SUBTASK && gUI.lastState == SUBTASK_AUXCUTSCENE &&
                       gUI.field_0x3 == 5 && gUI.nextToLoad == 2);
    if (gMain.task != TASK_GAME || gMain.state != GAMETASK_MAIN || !play || gRoomControls.reload_flags != 0)
        return;
    EntityData want[MAX_ARCHWAYS];
    ArchwayAt have[MAX_ARCHWAYS];
    const unsigned wanted = WantedCopies(want), had = ScanCopies(have, false);
    bool same = wanted == had;
    for (unsigned i = 0; same && i < wanted; ++i) {
        const s16 x = (s16)(want[i].xPos + gRoomControls.origin_x), y = (s16)(want[i].yPos + gRoomControls.origin_y);
        bool found = false;
        for (unsigned k = 0; k < had && !found; ++k)
            found = have[k].x == x && have[k].y == y && have[k].frame == (u8)want[i].type2;
        same = found;
    }
    if (same) return;
    if (had != 0) {
        /* Each deletion takes one off gEntCount when the slot is recycled: never below zero (see above). */
        if (gEntCount < had) gEntCount = (u8)had;
        ScanCopies(NULL, true);
    }
    for (unsigned i = 0; i < wanted; ++i) LoadRoomEntity(&want[i]);
}

void Port_NeighbourRooms_Update(void) {
    /* NEIGHBOURS: OFF leaves only the room (and takes the archway copies away). A change of the setting starts the
     * room's build again. */
    const int mode = Port_Config_GetNeighbourRooms();
    if (mode != sMode) {
        sMode = mode;
        sKey = 0xffffffffu;
    }
    if (mode == 0) {
        sActive = 0;
        sDrawn = 0;
        UpdateArchways();
        return;
    }
    const u32 key = RoomKey();
    if (key != sKey) {
        sKey = key;
        sTicks = 0;
    } else {
        ++sTicks;
    }
    /* At once, for a border scroll (the incoming room's neighbours show from its first frame; both rooms share
     * the tile set, so the layers' definitions already fit); again once a door's new tile set is in place, and
     * later in case. */
    const int closed = CurtainClosed();
    const int curtainChanged = closed != sCurtainClosed;
    sCurtainClosed = closed;
    if (sTicks == 0 || sTicks == 8 || sTicks == 120 || curtainChanged) {
        Build();
        sActive = 1;
    }
    UpdateArchways();
}

bool Port_NeighbourRooms_RoomJustChanged(void) {
    /* The room's numbers change before this module's tick runs: until it does, its list is the old room's. */
    return Port_Config_GetNeighbourRooms() != 0 && (RoomKey() != sKey || sTicks == 0);
}


int Port_NeighbourRooms_DrawnRects(PortBlurRect* out, int max, int scrollX, int scrollY) {
    if (!sActive || max <= 0) return 0;
    PortBlurRect found[MAX_ROOMS];
    int count = 0;
    for (unsigned j = 0; j < MAX_ROOMS; ++j) {
        if (((sDrawn >> j) & 1) == 0 || j == gRoomControls.room) continue;
        const RoomResInfo* nb = &gArea.roomResInfos[j];
        /* The part of the room that shows, in view pixels. */
        const int x0 = (int)nb->map_x - scrollX, y0 = (int)nb->map_y - scrollY;
        const int x1 = x0 + nb->pixel_width, y1 = y0 + nb->pixel_height;
        PortBlurRect r = { x0 > 0 ? x0 : 0, y0 > 0 ? y0 : 0, 0, 0 };
        r.w = (x1 < 400 ? x1 : 400) - r.x;
        r.h = (y1 < 240 ? y1 : 240) - r.y;
        if (r.w <= 0 || r.h <= 0) continue;
        int k = count++;
        while (k > 0 && found[k - 1].w * found[k - 1].h < r.w * r.h) { /* largest first */
            found[k] = found[k - 1];
            --k;
        }
        found[k] = r;
    }
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) out[i] = found[i];
    return count;
}

uint16_t Port_NeighbourRooms_Tile(int topLayer, int column, int row, uint16_t blank) {
    if (!sActive) return blank;
    const int c = column + MARGIN_X, r = row + MARGIN_Y;
    if (c < 0 || c >= WIN_COLS || r < 0 || r >= WIN_ROWS) return blank;
    const uint16_t entry = sWin[topLayer ? 1 : 0][r][c];
    return entry == NO_TILE ? blank : entry;
}

/* The 8x8 cells of the band beside room r along `axis` (the rows above and below it for y, the columns left and
 * right of it for x) between `start` and `start + view`, within the room's span on the other axis, that a
 * drawable neighbour covers. */
static int Coverage(const RoomResInfo* r, int axis, int start, int view) {
    const int pos = axis ? r->map_y : r->map_x, size = axis ? r->pixel_height : r->pixel_width;
    const int across0 = axis ? r->map_x : r->map_y, across1 = across0 + (axis ? r->pixel_width : r->pixel_height);
    const u64 connected = Connected((unsigned)(r - gArea.roomResInfos));
    int covered = 0;
    for (int a = start; a < start + view; a += 8) {
        if (a >= pos && a < pos + size) continue; /* the room itself */
        for (int b = across0; b < across1; b += 8) {
            const int x = axis ? b : a, y = axis ? a : b;
            for (unsigned j = 0; j < MAX_ROOMS; ++j) {
                const RoomResInfo* nb = &gArea.roomResInfos[j];
                if (nb == r || nb->pixel_width == 0 || ((connected >> j) & 1) == 0 || nb->tileSet != r->tileSet ||
                    nb->tiles != r->tiles)
                    continue;
                if (x >= nb->map_x && x < nb->map_x + nb->pixel_width && y >= nb->map_y &&
                    y < nb->map_y + nb->pixel_height) {
                    ++covered;
                    break;
                }
            }
        }
    }
    return covered;
}

/* The camera asks every frame: each candidate's coverage is kept per area, room, axis and view size. */
enum { REST_CANDIDATES = 64 };
static struct {
    u32 key;
    int first, count, centred;
    uint16_t score[REST_CANDIDATES];
} sRestCache[4];
static unsigned sRestNext;

static void ComputeRest(unsigned room, int axis, int view, int* first, int* count, int* centred, uint16_t* score) {
    const RoomResInfo* r = &gArea.roomResInfos[room < MAX_ROOMS ? room : 0];
    const int pos = axis ? r->map_y : r->map_x, size = axis ? r->pixel_height : r->pixel_width;
    *centred = pos - (view - size) / 2;
    *first = pos + size - view;
    *count = 0;
    if (size >= view || size == 0) return;
    for (int start = *first; start <= pos && *count < REST_CANDIDATES; start += 8)
        score[(*count)++] = (uint16_t)Coverage(r, axis, start, view);
}

static unsigned RestSlot(unsigned room, int axis, int view) {
    const u32 key = ((u32)gRoomControls.area << 24) | ((room & 0xFFu) << 16) | ((u32)(axis & 1) << 15) | ((u32)view & 0x7FFFu);
    unsigned slot = sizeof(sRestCache) / sizeof(sRestCache[0]);
    for (unsigned i = 0; i < sizeof(sRestCache) / sizeof(sRestCache[0]); ++i)
        if (sRestCache[i].key == key && key != 0) slot = i;
    if (slot == sizeof(sRestCache) / sizeof(sRestCache[0])) {
        slot = sRestNext;
        sRestNext = (sRestNext + 1) % (sizeof(sRestCache) / sizeof(sRestCache[0]));
        sRestCache[slot].key = key;
        ComputeRest(room, axis, view, &sRestCache[slot].first, &sRestCache[slot].count, &sRestCache[slot].centred,
                    sRestCache[slot].score);
    }
    return slot;
}

int Port_NeighbourRooms_Rest(unsigned room, int axis, int view, int prefer) {
    const unsigned slot = RestSlot(room, axis, view);
    const int centred = sRestCache[slot].centred;
    if (sRestCache[slot].count == 0 || Port_Config_GetNeighbourRooms() == 0) return centred;
    const int target = prefer == PORT_NEIGHBOUR_NO_PREFERENCE ? centred : prefer;
    int best = centred, bestScore = -1, bestDistance = 0x7fffffff;
    /* The centred position is a candidate too (it may fall between the 8-pixel steps). */
    for (int i = -1; i < sRestCache[slot].count; ++i) {
        const int start = i < 0 ? centred : sRestCache[slot].first + i * 8;
        const int score = i < 0 ? -1 : sRestCache[slot].score[i];
        const int d = start > target ? start - target : target - start;
        if (score > bestScore || (score == bestScore && d < bestDistance)) {
            best = start;
            bestScore = score;
            bestDistance = d;
        }
    }
    /* No neighbour anywhere: centred, as before P4.9. */
    if (bestScore == 0) return centred;
    return best;
}

/* The 8x8 cells of a world rectangle that a drawable room (`ref` itself, or one with its tile set) covers. */
static int ViewCovered(const RoomResInfo* ref, int x0, int y0, int w, int h) {
    const RoomResInfo* rooms[MAX_ROOMS];
    unsigned n = 0;
    for (unsigned j = 0; j < MAX_ROOMS; ++j) {
        const RoomResInfo* r = &gArea.roomResInfos[j];
        if (r->pixel_width == 0 || (r != ref && (r->tileSet != ref->tileSet || r->tiles != ref->tiles))) continue;
        if (r->map_x >= x0 + w || r->map_x + r->pixel_width <= x0 || r->map_y >= y0 + h || r->map_y + r->pixel_height <= y0)
            continue;
        rooms[n++] = r;
    }
    int covered = 0;
    for (int y = y0; y < y0 + h; y += 8)
        for (int x = x0; x < x0 + w; x += 8)
            for (unsigned k = 0; k < n; ++k)
                if (x >= rooms[k]->map_x && x < rooms[k]->map_x + rooms[k]->pixel_width && y >= rooms[k]->map_y &&
                    y < rooms[k]->map_y + rooms[k]->pixel_height) {
                    ++covered;
                    break;
                }
    return covered;
}

int Port_NeighbourRooms_LineUp(unsigned room, int axis, int view, int prefer, unsigned fromRoom, int otherStart,
                               int otherView) {
    const unsigned slot = RestSlot(room, axis, view);
    const int count = sRestCache[slot].count;
    int maxScore = 0;
    for (int i = 0; i < count; ++i)
        if (sRestCache[slot].score[i] > maxScore) maxScore = sRestCache[slot].score[i];
    /* Nothing to draw beside the incoming room (or NEIGHBOURS is OFF): as before P4.9. */
    if (count == 0 || maxScore == 0 || Port_Config_GetNeighbourRooms() == 0) return sRestCache[slot].centred;
    const RoomResInfo* from = &gArea.roomResInfos[fromRoom < MAX_ROOMS ? fromRoom : 0];
    int best = sRestCache[slot].centred, bestCovered = -1, bestDistance = 0x7fffffff;
    for (int i = 0; i < count; ++i) {
        if (sRestCache[slot].score[i] != maxScore) continue;
        const int start = sRestCache[slot].first + i * 8;
        const int covered = axis ? ViewCovered(from, otherStart, start, otherView, view)
                                 : ViewCovered(from, start, otherStart, view, otherView);
        const int d = start > prefer ? start - prefer : prefer - start;
        if (covered > bestCovered || (covered == bestCovered && d < bestDistance)) {
            best = start;
            bestCovered = covered;
            bestDistance = d;
        }
    }
    return best;
}

PortNeighbourStats Port_NeighbourRooms_Stats(void) {
    return sStats;
}
