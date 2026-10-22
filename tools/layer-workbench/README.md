# Layer workbench: room data extractor

Stage 1 of a desktop layer workbench for The Minish Cap, like the ones in
`../mzm/tools/layer-workbench` and `../sm-3ds/tools/layer-workbench`: the data
extractor and its tests. Stage 2 is the read-only HTML viewer (`serve.py` + `index.html`, see "Viewer").
Stage 2b adds sprites as pictures (`sprites.py` -> `sprites.json`, see "Sprites"): a curated rule per family
where the init code is known, and the default sprite of the definition table for every other object, enemy, NPC and
projectile. Stage 3 adds two editing aids, neither of which touches the game itself: a sprite picker (preview only,
choices kept in `sprite_view.json`) and a 3D DEPTH panel that edits the port's `port_sprite_depth.inc` (see "Sprite
picker" and "3D depth panel"). A ZONES window edits the port's `port_zone_depth.inc` (see "Zones").

```sh
export TMC_ROM=/path/to/the-minish-cap.gba    # USA (BZME) or EU (BZMP)
python3 tools/layer-workbench/build_maps.py   # writes maps.json, maps.json.gz, sprites.json, sprites_tiles.json.gz (~7 s)
python3 tools/layer-workbench/test_maps.py    # ~2.3 million checks (~6 s)
python3 tools/layer-workbench/test_depth_inc.py   # the .inc reader/writer and the two write endpoints (temp copies only)
python3 tools/layer-workbench/test_zone_inc.py    # the zone .inc reader/writer, its validation and /zone-depth (temp copies only)
python3 tools/layer-workbench/render_room.py 0x20 0 /tmp/room.png [--entities]
```

`--rom PATH` works in place of `TMC_ROM`. Python 3 standard library only;
`render_room.py` and the visual test also use PIL when it is installed.

**`maps.json`, `sprites.json` and `sprites_tiles.json.gz` are the game's art** (tiles, palettes, maps, sprite
pictures and tile data), and so is any PNG rendered from them. They are gitignored here; never commit, package or share them.
The repository's top `.gitignore` hides `*.py`; this folder's `.gitignore`
un-hides its scripts, so `git add tools/layer-workbench` picks them up.

## Files

| File | What |
|---|---|
| `tmcdata.py` | Readers: the ROM (pointer tables, LZ77, map definitions, palette groups), the enums in `include/`, the label layout of `data/map/entity_headers.s`, the entity loads in `src/roomInit.c`. |
| `build_maps.py` | Walks every area and room and writes `maps.json` (and, through `sprites.py`, `sprites.json`; `--no-sprites` skips it). |
| `sprites.py` | The OBJ side: object and NPC definitions, sprite/animation/frame tables, OAM pieces, tiles and palettes read from the ROM, the per-family rules (which animation/frame/flip the entity's init code picks) and the default sprite of every other entity; also the catalog of animations and frames the picker draws alternatives from. |
| `entdefs.py` | The enemy and projectile definition tables (`src/enemy.c`, `src/projectile.c`): a small C preprocessor and table parser, and the ROM lookup by content. |
| `depth_inc.py` | Reader and minimal-diff writer of `platform/3ds/source/port_sprite_depth.inc`. |
| `zone_inc.py` | Reader and minimal-diff writer of `platform/3ds/source/port_zone_depth.inc` (same design as `depth_inc.py`), with the zone checks the server runs on save. |
| `render_room.py` | Draws one room from `maps.json` as the BG hardware would. Also the reference for the viewer. |
| `test_maps.py`, `test_depth_inc.py`, `test_zone_inc.py` | The checks (below). |
| `serve.py`, `index.html` | The desktop viewer (see "Viewer"); `serve.py` also serves the three write endpoints. |
| `sprite_view.json` | The sprite picker's choices (committed, no art; created by the first choice). |

## How the data is read

From the retail ROM, the way the game reads it, not from the decomp's compiled
tables (which are the USA layout; the port remaps them for EU). The table
offsets per region are parsed from `kRomOffsets_USA` / `kRomOffsets_EU` in
`port/port_rom.c`, so there is no second copy of them.

| Game function | What it gives |
|---|---|
| `InitRoomResInfo` (`src/gameUtils.c`) | `gAreaRoomHeaders` (position, size, tileset id), `gAreaTileSets`, `gAreaRoomMaps`, `gAreaTiles`, `gExitLists`, `gAreaTable` (properties) |
| `LoadRoomTileSet` (`src/playerUtils.c`) | tileset: VRAM loads + palette group; metatile definitions and tile types; BG animations |
| `LoadRoomGfx` (`src/playerUtils.c`) | room maps; BG control per area; whether BG1 is shown |
| `LoadRoom` (`src/room.c`) | entity lists in properties 1, 0, 2 (after `TABIDACHI`), tile entities in property 3 |
| `LoadGfxGroups` (`src/gameUtils.c`) | the palette groups every area shares (11 and 12), loaded before the tileset's |

Two kinds of room:

- **metatile** (562 rooms): BG2 is `gMapBottom`, BG1 is `gMapTop`; each is a
  `width/16 x height/16` map of metatile indices into the area's metatile sets
  (`bottom` for BG2, `top` for BG1). A metatile is four BG entries (top-left,
  top-right, bottom-left, bottom-right), as `RenderMapLayerToSubTileMap` copies
  them.
- **special** (35 rooms: the Minish houses, the town's Minish holes, the beanstalk
  climbs): the room's bottom map is `gAreaRoomMap_None` (first entry 0xFFFF) and
  the picture is a raw BG tilemap (`gMapDataBottomSpecial` / `TopSpecial`), in
  32x32-entry chunks that `sub_0807C5F4` places left-top, right-top, left-bottom,
  right-bottom. The extractor places them the same way and emits the room's part.

BG control (`LoadRoomGfx`'s switch on the area, values from `gUnk_080B77C0`):

| Areas | BG2 (`gMapBottom`) | BG1 (`gMapTop`) |
|---|---|---|
| default | `0x1C42`: priority 2, char base 0, 16 colours | `0x1D45`: priority 1, char base 1 |
| Minish house interiors, town Minish holes | `0x1CC2`: 256 colours | hidden (DISPCNT bit cleared) |
| house interiors 1-4, tree interiors, dojos, Minish cracks, Wind Tribe tower, Ezlo cutscene | `0x1C42` | `0x1D46`: priority 2 |

`0x1C42` has the mosaic bit set; the mosaic register is normally 0, so it has
no effect. Equal priorities: the lower BG number is in front, so BG1 is always
over BG2. Sprites (OBJ) with priority *p* are drawn over BGs with priority >= *p*.

## `maps.json` schema (version 1)

Big binary blocks are base64 of little-endian data; everything else is plain JSON.

```js
{
  "format": "tmc-layer-workbench-maps", "version": 1,
  "rom": { "region": "EU", "gameCode": "BZMP", "sha1": "..." },
  "enums": {
    "entityKinds": { "1": "PLAYER", "3": "ENEMY", ... },          // include/entity.h
    "ids": { "ENEMY": { "94": "CURTAIN", ... }, "OBJECT": {...},    // include/enemy.h, object.h,
             "NPC": {...}, "MANAGER": {...}, "PROJECTILE": {...},   // npc.h, manager.h,
             "PLAYER_ITEM": {...} },                                // projectile.h, playeritem.h
    "tileEntityTypes": {...}, "areas": {...}, "warpTypes": {...}, "transitionTypes": {...}
  },
  "gfx": {                       // shared by rooms, deduplicated
    "g0": {
      "vram": "<base64, 65536 bytes>",     // BG VRAM 0x06000000-0x0600FFFF as loaded
      "palettes": "<base64, 1024 bytes>",  // 512 BGR555 colours: BG 0-255, then OBJ 256-511
      "paletteLoaded": 2147450879,         // bit n: palette n (0-15 BG, 16-31 OBJ) was loaded
      "paletteGroups": [11, 12, 28],       // in load order
      "vramLoads": [{ "vram": 0, "size": 16384 }, ...],
      "bgAnimations": [2, 3, ...]          // first frame applied (see below)
    }
  },
  "metatiles": {                 // per area id
    "0": { "bottom": "<base64 u16, 4 per metatile>", "top": "...",
           "bottomTypes": "<base64 u16 per metatile>", "topTypes": "..." }
  },
  "areas": [ { "id": 32, "name": "AREA_MINISH_HOUSE_INTERIORS", "rooms": [ room, ... ] } ],
  "skippedRooms": [ { "area": 2, "room": 1, "reason": "no tileset, no room map" } ],
  "warnings": [], "stats": { "specialTilesInMaps": 0 }
}
```

A room:

```js
{
  "id": 0, "name": "GENTARI_MAIN", "enumName": "ROOM_MINISH_HOUSE_INTERIORS_GENTARI_MAIN",
  "worldX": 336, "worldY": 112,          // area coordinates of the room's top-left, pixels
  "width": 240, "height": 160,           // pixels
  "tileSetId": 0, "gfx": "g33", "metatiles": "32",
  "mode": "special",                      // or "metatile"
  "layers": {
    "BG2": { "source": "gMapBottom", "visible": true, "defined": true, "loaded": true,
             "bgcnt": 7362, "priority": 2, "charBase": 0, "colors": 256, "screenBase": 28, "mosaic": true,
             "format": "tilemap", "w": 30, "h": 20, "data": "<base64 u16>" },
    "BG1": { ..., "visible": false, "format": "tilemap", "w": 30, "h": 20, "data": "..." },
    // in a metatile room: "format": "metatile", "metatileSet": "top", "w": width/16, "h": height/16
    "BG3": { "visible": false, "format": "tilemap", "w": 32, "h": 32, "bgcnt": null, "note": "..." }
  },
  "drawOrder": ["BG2"],                   // visible layers, back to front
  "properties": "Room_MinishHouseInteriors_GentariMain",   // label of the property table
  "callbacks": ["sub_unk3_...", "sub_StateChange_..."],    // property slots 4-7 (functions)
  "entities": [ entity, ... ],
  "tileEntities": [ tileEntity, ... ],
  "unresolvedLists": [ { "loadedBy": "...", "ref": {...}, "reason": "..." } ],
  "exits": [ exit, ... ]
}
```

- `data` of a `metatile` layer: `w*h` metatile indices, row-major; an index
  below 0x4000 picks four BG entries from `metatiles[area][metatileSet]` at
  `index*4`. Of a `tilemap` layer: `w*h` BG screen entries.
- A BG screen entry: bits 0-9 tile, 10 h-flip, 11 v-flip, 12-15 palette (16-colour
  layers only). A 16-colour tile is 32 bytes at `charBase*0x4000 + tile*32`,
  4 bits per pixel, low nibble first; colour 0 is transparent, the rest is
  `palettes[pal*16 + c]`. A 256-colour tile is 64 bytes at `charBase*0x4000 +
  tile*64`, colour index straight into the 256 BG colours. Where every layer is
  transparent the backdrop shows: BG colour 0 (black, `SetColor(0, 0)`).
- `visible: false` means the game does not show the layer after loading the room:
  BG1 in the 256-colour areas, BG3 always (room managers turn BG3 on and set its
  control at runtime), and any layer with `defined: false`, which the room leaves
  as the previous room had it (`note` says why: a bottom map the room does not
  load, or an area without that metatile set).
- BG3 appears only when the tileset writes VRAM screen block 30 (0xF000), BG3's
  map. Its char base and priority are set by whichever manager shows it, so
  `bgcnt` is null.

An entity (`EntityData`, 16 bytes; `LoadRoomEntity` in `src/room.c`):

```js
{ "list": "property0", "index": 1, "when": "always", "label": "Entities_MinishHouseInteriors_GentariMain_0",
  "kind": 6, "kindName": "OBJECT", "id": 88, "idName": "GENTARI_CURTAIN",
  "type": 0, "type2": 0, "timer": 0, "paramB": 0, "paramC": 8585216,
  "x": 24, "y": 80, "positioned": true,
  "collision": 0, "layer": null, "pool": 15, "spawn": 0, "raw": "060f5800..." }
```

- `kind` = byte 0 low nibble; `collision` = its high nibble (1: layer 1, 2: layer 2,
  else the game resolves the layer from the tile: `layer` null).
- `id` is what the decomp's macros call `subtype`, `type` their `paramA`. `paramB`
  is the u32 at byte 4: its low byte becomes `entity->type2`, the next one
  `entity->timer`. `paramC` is the u32 at byte 12 (a script pointer for
  scripted entities, flags for others).
- `pool` = byte 1 low nibble (the entity list); `spawn` = its high nibble:
  0 normal, 1 no position set, 2 captain, 4 runs a script, 5 only if not already present.
- `x`, `y` are room coordinates in pixels (the game adds the room origin).
  `positioned` is true when the game copies them into the entity's position
  (`sub_0804AF0C`); managers and spawn mode 1 use them as parameters. Some
  entities reinterpret them anyway (TORCH_TRAP keeps a parameter in `y`).
- `when`: `always` (properties 0 and 1), `afterTabidachi` (property 2, enemies
  loaded once the global flag `TABIDACHI` is set), `conditional` (loaded by one of
  the room's callbacks in `src/roomInit.c`; `loadedBy` names it). The condition
  itself (story flags, items, region checks) is not evaluated.

A tile entity (`TileEntity`, 8 bytes; `LoadRoomTileEntities`): `type`, `typeName`,
`localFlag`, `b2`, `b3`, `tilePos`, `b6`, `b7`, `raw`, and `x`, `y` when the type has a
position: `SMALL_CHEST` and `DESTRUCTIBLE_TILE` keep a metatile index in
`tilePos` (x = (tilePos & 63) * 16, y = (tilePos >> 6) * 16, the tile's top-left);
`BIG_CHEST` and `BOMBABLE_WALL` keep the x pixel in `tilePos` and the y pixel in
bytes 6-7. Other types: `x`, `y` null.

An exit (`Transition`, 20 bytes, `include/transitions.h`): `warpType`(`Name`), `x`, `y`
(trigger), `destX`, `destY`, `shape`, `destArea`, `destRoom`, `layer`,
`transitionType`(`Name`), `facing`, `sfx`, `unk2`, `unk3`.

## Viewer

```sh
python3 tools/layer-workbench/serve.py [--rom PATH] [--port 8765] [--no-browser] [--rebuild]
                                       [--depth-inc PATH] [--sprite-view PATH] [--zone-inc PATH]
```

Serves this folder on `127.0.0.1:8765` and opens the browser. `maps.json` (and
`sprites.json`, `sprites_tiles.json.gz`) are built first (`TMC_ROM` or `--rom`) when missing or older than
`build_maps.py` / `tmcdata.py` / `sprites.py`; a lone `maps.json.gz` is accepted too,
and the viewer works without `sprites.json` (markers only). Standard library only, and one
self-contained `index.html` (no CDN). Opened from `file://` the page cannot read
`maps.json`: choose or drop it (or the `.gz`) on the prompt it shows.

`index.html?room=AA-RR` (hex, the console's area/room) opens a room directly; the
address follows the room you open. `index.html?test` runs the self-checks: counts,
and the composite of 14 rooms (all layers, flips, palettes, special mode) hashed
against `render_room.py`'s output, the pictures of `sprites.json` (hashes in
`EXPECT_SPR`, curated and default), that the viewer's own `renderFrame` reproduces every distinct
picture, the picker's transforms and precedence, the depth panel's logic (first match, what is and is not
written, the illustration's direction, the jump), the ZONES window's (the checks, what SAVE sends, snapping, the outline and the
illustration, COPY INFO, the jump), that RESULT draws a sprite when SPRITES is on, and that a hidden layer's
window starts minimised. They never write a file (use `serve.py --depth-inc TEMP --zone-inc TEMP` anyway when running them); the result shows in a box and as the page title
(`TEST PASS`). After changing `build_maps.py`, `sprites.py` or `render_room.py`,
regenerate the hashes in `EXPECT` / `EXPECT_SPR`.

The compositing is a JavaScript port of `render_room.py`: each layer is decoded
lazily per room (base64 on demand, cached for the last 10 rooms and per gfx set), then
layers are drawn back to front (higher priority number first, higher BG number first
on ties). The result is pixel-identical to `render_room.py` on the 14 rooms checked.

### Windows

Floating windows: drag by the header, resize from the corner, `_` minimises (to the
bottom bar, which also re-opens any window), `x` hides. Positions, sizes and states are
kept in `localStorage`; RELAYOUT restores the default arrangement.

| Window | What |
|---|---|
| RESULT | The room composed, with the sprites over it (SPRITES) and, when the 3D DEPTH window is open, the depth illustration of the selected entities; a checkbox per layer (BG1/BG2/BG3). Layers the game hides in that room are marked and need "show layers the game hides". |
| BG2 / BG1 / BG3 LAYER | One layer alone over a checkerboard. BG3's control is set by managers at runtime, so it has a char-base selector (a guess to try). A layer the game does not show in the room (`visible: false`, e.g. BG1 of the Minish houses) starts **minimised** (RELAYOUT and room changes keep that rule; it reopens in the next room that shows it) and, when opened, is drawn dimmed under a "NOT SHOWN BY THE GAME IN THIS ROOM, data may not be a drawable map" notice. RESULT keeps ignoring such layers unless "show layers the game hides" is ticked. |
| MAP | The rooms of the current area at their world positions; click opens; the current room is outlined; visited rooms show their picture. |
| ENTITIES | Markers switch, SPRITES switch, text filter, one checkbox per kind (ENEMY, OBJECT, NPC, MANAGER, ..., plus TILE for tile entities and EXIT), and the list. Click selects (Ctrl adds), double click centres the views on it. |
| INFO | The selection: for an entity with a sprite record, the **sprite picker** (below), the picture and **why it is drawn that way** (the rule and the repo function it comes from, or "default (not curated)" and the definition table; animation/frame, flips, offset, tiles, palette, and the runtime state the pose assumes); then every field of the objects, the metatile index, type and the four tile entries of each block (with a preview). COPY INFO and CLEAR SELECTION. |
| 3D DEPTH | The selected entity's stereo depth mode from `port_sprite_depth.inc`, a selector to change it, the list of all entries, SAVE (below). |
| ZONES | The port's zone depth fixes from `port_zone_depth.inc`: draw a rectangle, the room's zones and all zones, edit, SAVE (see "Zones"). Starts minimised (the bottom bar opens it). |

The left list groups rooms by area (collapsible, remembered); search takes a name, a
hex area (`48`, `0x48`) or `AA-RR`. The top bar shows area/room, size, mode, each layer's
BGCNT (priority, char base, colours), the draw order and the entity counts.

Markers: filled square = positioned entity; dashed diamond = `x`,`y` are parameters,
not a position (`positioned` false: managers, spawn mode 1); red triangle on the room
edge = the position lies outside the room; round = tile entity; white square = exit
trigger. Labels read `KIND/ID type`. Sprites are drawn in RESULT only (the layer
windows show one layer); the markers stay, switch them off with ENTITIES / markers.

### Shortcuts

| Input | Action |
|---|---|
| Ctrl+wheel | zoom at the cursor (wheel / Shift+wheel pan) |
| Shift+drag, middle drag | pan |
| click | select the marker under the cursor, else the block (the layer itself, or in RESULT the topmost layer with a pixel there); Alt+click forces a block |
| drag | rectangle: blocks (and the markers inside) |
| Ctrl+click / Ctrl+drag | add to / toggle in the selection |
| arrows | move the block selection; Esc clears |
| `[` `]` | previous / next room; `/` focuses the search |
| E, P, L, G, Y, F, B | entity markers, sprites, labels, grid, sync pan, fit, room list |
| Ctrl+C with no text selected, COPY INFO | room + selection as plain text for a chat (blocks up to 256) |

Entities without a picture keep their marker. Clicking a sprite selects its entity. If you opened the viewer before the 3D DEPTH window existed,
RELAYOUT puts the windows in the new default arrangement.

Panning and zooming the image windows is synchronised (SYNC PAN, on by default); the
MAP window is independent. Metatile indices past the loaded set are flagged in the top
bar and in INFO. Read-only: editing and saving are a later stage.

## Sprites

Entities are positions in `maps.json`; what they look like is decided by their own
init code. `sprites.py` draws two kinds of picture and writes `sprites.json`
(`tmc-layer-workbench-sprites`, version 2):

- **curated**: a family with a rule (`RULES`) that states which animation, frame, flips and offset its init
  code picks, citing the function;
- **default (not curated)**: every other positioned `OBJECT`, `ENEMY`, `NPC` and `PROJECTILE` whose kind has a
  definition table in the ROM gets the table's sprite, palette and tiles, drawn as animation 0's first frame with
  no entity flips. INFO says "DEFAULT (not curated)" and names the table. It is the sprite the entity is *made
  of*, not necessarily the pose its code shows (an octorok faces down here whatever it does in play): use the
  picker for the other animations. A curated rule always wins over the default.

```js
{ "version": 2, "pictures": { "<hash>": { "w": 32, "h": 64, "ox": -14, "oy": -32, "rgba": "<base64 w*h*4>" } },
  "rooms": { "20-00": { "1": { "family": "OBJECT/GENTARI_CURTAIN", "pic": "<hash>", "src": "<source>",
        "rule": "GentariCurtain_Init (src/object/gentariCurtain.c)", "sprite": 134,
        "anim": 0, "frame": 0, "flipX": 0, "flipY": 0, "baseFlipX": 0, "baseFlipY": 0, "dx": 2, "dy": 0,
        "gfx": "swapped, frame 0 of sprite 134", "palette": 132,
        "why": ["x += 2 (GentariCurtain_Init)", "..."], "state": "shut pose; ..." } } },
  "sizeTable": [240 numbers],                    // OBJ size table: piece anchors and sizes per flip
  "sprites": { "134": { "frames": { "0": [[x, y, shape, size, flags, tile, palAdd], ...] },   // OAM pieces per listed frame
                        "anims": [ { "f": [[frameIndex, duration, settings, flags], ...], "l": loopTarget } ],
                        "swap": { "0": [numTiles, firstTile] } } },   // swapped sprites: tiles per frame
  "sources": { "<key>": { "s": 134, "m": 1, "t": "f12", "r": [[16 colours]...], "a": [6 absolute OBJ rows] } },
  "tilesets": { "f12": "<base64 of a fixed gfx blob>" },
  "notCovered": { "OBJECT/SPECIAL_CHEST": "reason" }, "stats": {}, "skips": {} }
```

Room keys are the viewer's (`AA-RR`, hex); the inner key is the entity's index in the room's `entities`. A picture
is pre-rendered RGBA (the OAM pieces of the frame, tiles and palette resolved, flips applied) whose top-left is
(`ox`, `oy`) from the entity's `x`, `y`; pictures are deduplicated (445 for ~2900 entities). A record with
`skipped` instead of `pic` says why it has none. `src` points at what the picker needs to draw any other frame of
the same sprite the way this entity would: the sprite's frames and animations, the tiles (a fixed gfx blob, or the
sprite's own swapped tiles), and the palette rows. The swapped tiles are 15 MB of base64 and live in
`sprites_tiles.json.gz`, loaded the first time a picture needs them; `sprites.json` itself is ~7 MB. The viewer's
`renderFrame` is a port of `render_pieces` and is checked against every picture (below).

How a sprite is drawn, and where `sprites.py` reads each step (the ROM, the way the
port reads it, never the decomp's compiled USA tables):

| Step | Source |
|---|---|
| sprite index, OBJ palette id, gfx index and type | **objects**: `gObjectDefinitions[id]` (`LoadObjectSprite`, `src/objectUtils.c`); **NPCs**: `gNPCDefinitions[id]` (`NPCInit`, `src/npcUtils.c`); both 8 bytes per record, not in `RomOffsets`, so found by content in the ROM (the first run of four plain records) and compared entry by entry with `src/objectDefinitions_data.inc` / `src/npcDefinitions_data.inc` (EU pass) by the tests; a MULTI_FORM entry points at a table of forms picked by the entity's type, whose length comes from the source (a type past it is left as a marker). **Enemies and projectiles**: `gEnemyDefinitions` (`EnemyInit` / `LoadEnemySprite`, `src/enemy.c`) and `gProjectileDefinitions` (`src/projectile.c`), 16 bytes per record (`entdefs.py`): the C source is preprocessed for the ROM's region, the ROM is searched for the whole table with the hitbox pointer and sprite index as wildcards (exactly one match), then every record and form is compared with the source and the sprite indices with `enum Sprites` in the EU layout. `gfx` there: 0 none, bit 15 tiles already in VRAM, bit 14 swapped, else a fixed gfx index |
| animation / frame / flips / offset | curated: the family's init code (`RULES`); default: animation 0 |
| first frame of an animation | `InitializeAnimation` / `FrameZero` (`port/port_animation.c`): `gSpritePtrs[sprite].animations[anim]`, 4-byte frames (frame 0xFF draws nothing; bit 7 of the flags byte ends the animation and the next byte is the loop-back distance); the frame's spriteSettings flip bits XOR the entity's |
| OAM pieces | `gFrameObjLists[sprite]` -> frame table -> `{count, 5 bytes per piece}` (`LookupFrameData`, `RenderSpritePieces`, `port/port_draw.c`); the OBJ size table (`overlaySizeTable`) gives anchors, which is how a flip mirrors a piece |
| tiles | type 0: `gFixedTypeGfxData[gfx]` (`LoadFixedGFX`, `sub_080ADDD8`); type 1: the frame's tiles from `gSpritePtrs[sprite]` (`UpdateSpriteGfxSlot`); type 2 (already in VRAM) is not modelled |
| palette | `LoadObjPalette` / `LoadObjPaletteAtIndex` (`src/color.c`): ids 0-5 are the room's OBJ rows 0-5, 6-0x14 BG palette id-6 of the room, 0x16+ `objPalettes` in the ROM; a piece's palette addend selects the row |

Curated families (EU ROM, rest pose; see each record's `state` for what it assumes):
`BOSS_DOOR` (animation `type & 0x1f`), `LOCKED_DOOR` (`frameIndex = type & 3`, `|= 4`
for key locks; gfx by area from `gUnk_0811F690`, read from the ROM), `MINECART_DOOR`
(same gfx; `frameIndex = type`), `METAL_DOOR` (`flipY`), `GENTARI_CURTAIN` (animation 0,
x + 2), `CHEST_SPAWNER` types 2-5 (animation 0), `LIGHT_DOOR` type 0, `TRAPDOOR`
(animation 0).

Left as markers (a wrong picture is worse than a marker; the reason is in INFO and `skips`). Curated families
the rules do not draw: `SPECIAL_CHEST` (no sprite in its definition), `CHEST_SPAWNER` types 0, 1, 6, 7 (a BG tile
set at runtime), `HOUSE_DOOR_EXT/INT` (spawners driven by the room's property table; their default frame depends
on runtime state, so they are in `NOT_COVERED` and get no default either), `ROTATING_TRAPDOOR` (transient). Default
sprites: 1337 of 4052 candidate entities, by reason: tiles already in VRAM, loaded by something else (object gfx
type 2: 646; enemy common gfx: 26; an enemy gfx of 0: 13), the animation starts on frame 255 (draws nothing: 150) or on a frame past
its sprite's frame table (20), the definition has no sprite (136 objects and NPCs: drawn by their own code), a frame
without OAM pieces (132), a palette that is runtime state or an absolute OBJ row the room did not load (140), a fixed gfx index with no
data (23), a position outside the room (51, the data quirks below). Small and big chests (`SMALL_CHEST`, `BIG_CHEST` tile entities) are BG tiles already in the maps, not sprites.

The frames the picker lists are the ones an animation names, the ones a record draws and, for a sprite without
animations, its first raw frames. The frame tables of `gFrameObjLists` have no stored length: they are laid out
one after the other, so a table runs up to the next sprite's table (where the order breaks, mostly sprites past
330, the length is unknown); a frame index past that reads another sprite's table, so such frames are not listed.

Adding a family: write a rule (cite the init function), add the regexes it relies on
to `RULE_SOURCES`, run `build_maps.py`, `test_maps.py`, and paste the new counts and
checksums after looking at the rooms.

## Sprite picker

In INFO, for a selected entity that has a drawn sprite (curated or default), the **SPRITE PICKER** shows, under the
line *PREVIEW ONLY: these choices change what this viewer draws. They do not change the game*:

- **animation** and **frame** dropdowns: every animation of the entity's sprite (with its step count and loop), and the
  steps of the chosen one (frame, duration, the frame's own flips); or the frames listed for the sprite (raw);
- **FLIP H**, **FLIP V** (mirror about the entity's position) and **ROTATE 90** (clockwise, about the picture's
  centre): previews only, applied on top of what the game does for that frame (the entity's own flips XOR the
  frame's spriteSettings), so you can see how the sprite would look; the game never does that;
- **apply to**: every entity of this kind/id/type (default), every entity of this kind/id (any type), or this entity
  only (with several selected, the ones that use the same sprite; others are left alone). Precedence: this entity >
  exact type > any type > the default sprite;
- **RESET TO DEFAULT**: drops the entity's, its kind/id/type's and its kind/id's choices.

RESULT redraws the sprite at the entity's position at once, and INFO's `PREVIEW` line (and COPY INFO) says a choice is active.
Choices are saved by `serve.py` to `sprite_view.json` (`GET`/`POST /sprite-view`, atomic write, sorted keys so diffs
stay small) a moment after each change; without `serve.py` they stay in this browser only and the picker says so.
The file holds settings only, never art:

```js
{ "format": "tmc-sprite-view", "version": 1,
  "rules":    [ { "kind": "ENEMY", "id": "KEATON", "type": 0 /* null: any type */, "anim": 1, "step": 0, "flipX": 1, "rot": 90 } ],
  "entities": [ { "room": "20-00", "index": 1, "frame": 4 } ] }      // "frame": a raw frame instead of "anim"
```

None of it reaches the game: it exists so a sprite can be inspected, and so the depth panel can be tried on the sprite as
it would look in another orientation.

## 3D depth panel

The port's stereo depth for a sprite follows its entry in `platform/3ds/source/port_sprite_depth.inc`
(`PORT_SPRITE_DEPTH(kind, id, type, mode)`, `docs/stereo-3d.md`, "Sprite depth corrections"). The modes are `UP` (the
normal rule: the ground is the sprite's bottom edge and the depth grows upwards; what an entity without an entry
gets), `DOWN` (hangs from the top edge, the depth grows downwards; written `PORT_DEPTH_DOWN`, the old
`PORT_DEPTH_INVERT` is read as the same thing), `LEFT` (the right edge is the ground, the depth grows towards the left:
a sprite on a left wall), `RIGHT` (the mirror) and `FLAT` (no lean). **The port takes the first entry of the file
that matches** (`SpriteDepthMode`, `port/port_draw.c`: same kind and id, and the type equal or `PORT_ANY_TYPE`), not
the most specific one.

The **3D DEPTH** window works on the first selected entity that has a name:

- the **effective mode**, and whether it comes from an entry for exactly that type, from a `PORT_ANY_TYPE` entry (with its
  line in the file) or is `UP (default)`; an exact entry that sits after an any-type one is flagged as never reached;
- a selector (UP / DOWN / LEFT / RIGHT / FLAT) and **applies to**: this type only, or any type. Changing it edits a
  working copy at once. An entry that would only restate what the entity gets anyway is **not written** (an exact entry equal
  to the any-type one, UP for any type) unless "write even if it equals the default" is ticked;
- the **illustration** in RESULT: over the sprite's opaque pixels, a gradient from clear at the ground edge to red at the
  far edge, and an arrow along the direction the depth grows, labelled "illustration, not a render" (flat: a grey wash).
  It follows the flips and turns chosen in the sprite picker (tick off to keep it in screen terms), and can be shown for
  every entity of the room that has an entry;
- the **list of all entries**, in file order: click to jump to an example room (and select the entity), **x** to remove
  (marked changed / new / shadowed); lines the tool does not understand are listed read-only;
- **SAVE**, **REVERT** and an **UNSAVED CHANGES (n)** badge. SAVE sends the difference (what to set, what to remove) to
  `serve.py` (`GET`/`POST /sprite-depth`), which edits **the file as it is on disk now** through `depth_inc.py`, atomically.
  SAVE changes the port's next build; nothing in this viewer does.

`depth_inc.py` is a minimal-diff editor, not a serialiser: the header block, comments, blank lines, entries it cannot read
(an unknown mode or a symbolic type), the line ending and the final newline are kept byte for byte. A changed entry keeps
its indentation and trailing comment; `INVERT` stays `INVERT` unless its mode changes. The comment lines directly above an
entry (`//` or a one-line `/* */`, no blank line between; a multi-line block is the header and never attached) belong to
it: removing the entry removes them, changing it keeps them. A new entry goes after the last entry, but an exact type goes
just before the first any-type entry of its kind and id, because it would never be reached after it. Entries inside a
block comment are not entries.

`--depth-inc PATH` makes both the panel and the tests work on another copy of the file (`--sprite-view PATH` likewise for
the picker); `test_depth_inc.py` and any experiment should use a copy. The endpoints accept only `application/json`
POSTs whose Host is local, up to 1 MB, and only these two paths.

## Zones

The port can draw a rectangle of one layer at the stereo depth of another, for places where the layers' own planes look wrong
(the first case: Hyrule Town's log hut, a one-metatile hole in its BG1 roof through which BG2 shows a chick, which belongs at the
roof's plane). Each fix is one line of `platform/3ds/source/port_zone_depth.inc` (optional, hand-curated):

```
PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)      e.g. PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)
```

`area`, `room`: the room the fix belongs to (numbers, hex ok); `bg`: 1, 2 or 3, the layer whose pixels inside the rectangle are
moved; `x`, `y`, `w`, `h`: the rectangle in **room pixel coordinates** (the coordinates the viewer shows for the room, so the
status line and the zone agree); `toBg`: 1, 2 or 3, the layer whose stereo plane the rectangle takes. "Draw this rectangle of layer
`bg` at the depth of layer `toBg`." The file's header comment says the same; the writer creates a missing file with that header.

The **ZONES** window (starts minimised; the outline, the illustration and DRAW mode work while it is open):

- **DRAW mode**: tick it, then drag on RESULT or on a layer window (Shift+drag and the wheel still pan and zoom). The rectangle
  snaps to 8 px (a tile) or 16 px (a metatile) and covers every cell the drag touched, so a click is one cell; its size shows live
  and in the fields below, which can also be typed. Dragging on a layer window sets `bg` to that layer (on RESULT it stays what the
  selector says, BG2 at first); `toBg` is the other selector (BG1 at first, the other of BG1/BG2 when it would equal `bg`). ADD adds
  it to the room's zones; ADD is disabled while a check fails (below).
- **THIS ROOM** lists the room's zones, each outlined and tinted on the views (colour by `toBg`, label `BG2 -> BG1`; a layer window
  tints the zones of its own layer more). Click selects, jumps (centres the views) and opens its fields: x, y, w, h, bg, toBg, committed
  on change; **x** removes the zone. **ALL ZONES** lists every zone of every room; clicking one opens its room. Lines the tool does not
  understand are listed read-only.
- **SAVE**, **REVERT** and an **UNSAVED CHANGES (n)** badge, like the depth panel: SAVE sends the difference (what to set, what to remove
  by key `area, room, bg, x, y, w, h`; editing a rectangle is a remove plus a set) to `serve.py` (`GET`/`POST /zone-depth`), which
  edits the file as it is on disk now through `zone_inc.py`, atomically, and creates it with its header when missing.
- **Checks** (the window shows them per zone and the server repeats them on save): the room exists in `maps.json`, the rectangle lies
  inside the room (it may touch the edge), `bg` and `toBg` are 1..3 and different, `w`, `h` >= 1, the same rectangle is not listed
  twice; these are errors (they disable SAVE, and a POST gets a 400). A layer the room does not define (no data, e.g. BG3 of Hyrule
  Town) is only a warning.
- **Preview**: "outline the zones" draws every zone of the room as a dashed outline with its label (faint in RESULT). "RESULT: shift each
  zone's layer 2 px" is an illustration: inside each rectangle the layer `bg` is drawn 2 px to the side (right when `toBg` is a lower
  number, i.e. nearer, left otherwise), over the other layers in their order, labelled "illustration, not a render". It is an
  exaggerated hint that something changes plane there, not the stereo renderer's output (the console shows that, 3D WORLD on).
- **COPY INFO** adds the room's zones as `PORT_ZONE_DEPTH(...)` lines.

`zone_inc.py` is a minimal-diff editor like `depth_inc.py`: the header, comments, blank lines, entries it cannot read (symbolic or
octal-looking numbers, a missing argument), the line ending and the final newline are kept byte for byte; a changed entry keeps its
indentation, number spelling and trailing comment (only `toBg` changes in place); the comment lines directly above an entry belong to it
and go with it on removal; a new zone goes after the last zone of its room (else the last zone, else the end of the file); entries inside
a block comment are not entries. `--zone-inc PATH` points the window and the tests at another copy; `test_zone_inc.py` and any experiment
should use one. The endpoint has the same limits as the others (`application/json`, local Host, 1 MB). The server reads `maps.json` (or
the `.gz`) the first time it needs the room table; without one it skips the room checks.

## Decisions

- **Default sprites mark themselves.** A record made by the default path says `default (not curated)`, never carries a
  curated rule's `why`, and is skipped, with the reason, where its tiles or palette cannot be known; a curated rule
  always wins; a family in `NOT_COVERED` gets neither.
- **The catalog is sparse and the tiles are lazy.** A frame table has no stored length, so only frames something names are
  listed; the 15 MB of swapped tiles are a separate gzip file the viewer fetches on demand, which keeps `sprites.json` at ~7 MB.
- **Picker flips are bitmap mirrors about the entity's position** (identical to the game's own flip, checked on 1074
  frames), rotation is about the picture's centre; both are previews and are stored as settings only.
- **The depth writer never rewrites the file.** It applies set/remove operations to whatever is on disk, and it keeps the
  port's first-match rule in the way it places new entries.

- **The ROM, not the compiled tables.** The decomp's C/asm tables are the USA
  layout and the port remaps them for EU at runtime; walking the retail ROM's own
  pointers gives each region's real data (the EU lists do differ, e.g. extra
  enemies in Crenel's minish path, other flags in Royal Valley).
- **Entity list names** come from laying `data/map/entity_headers.s` out byte by
  byte with the region's `.ifdef`s (`.incbin` sizes from `assets/assets.json`),
  pinned at `gAreaTable[0]`. Every room's property table and every slot of it
  then has to land on the label the `.s` names, which the test checks.
- **Lists loaded by callbacks**: the bodies of the room's slot 4-7 functions in
  `src/roomInit.c` are scanned for `LoadRoomEntityList(&label)` and
  `LoadRoomEntityList(GetCurrentRoomProperty(n))`. Only the function body itself,
  not functions it calls. `LoadRoomEntity(&list[i])` (index known at runtime), the
  port's local arrays (`bombBag`, the Cloud Tops fights) and labels the region does
  not have (`gUnk_080F5B3C` in EU) go to `unresolvedLists`.
- **Palettes**: palette groups 11 and 12 and the black backdrop first (game task
  start), then the tileset's group, each followed by `sub_080533CC` (BG palette 2
  colours 0-4 from OBJ palette 2), then the first frame of each BG animation, then
  any palette or VRAM load in the room's own map definitions. A palette a room
  changes at runtime (fades, dark rooms, a manager) is not modelled.
- **BG animations** (water, torches, doorways): their first frame is applied, as
  the port primes it at room init and the GBA shows it after the first timer. The
  frame table `gUnk_080B7278` is not in `RomOffsets`; it is taken at the USA
  distance before `bgAnimTable` and validated (every entry a ROM pointer).
- **Skipped headers.** 20 room headers have no tileset or no map in the ROM's
  tables (Hyrule Town room 1, beanstalk rooms 7-12, unused areas) and the game
  never loads them; they are listed in `skippedRooms`. All-zero placeholder
  headers (tileset 0xFFxx) are dropped without a record.

## Checks (`test_maps.py`)

Plain Python; with the ROM (`--rom` / `TMC_ROM`) it also runs the two checks that
need it.

- Counts: 91 areas, 597 rooms, 35 special, 20 skipped headers, 128 gfx sets, 5135
  entities (813 from callbacks), no build warnings.
- Names: the enum counting agrees with the `/*0xNN*/` comments of the enemy and NPC
  enums; `LoadRoomGfx`'s switch, `gUnk_080B77C0` and `LoadGfxGroups`' palette
  groups still match what `build_maps.py` copies.
- Labels: every room's property table (from the ROM) lands on a label, and every
  slot holds the pointer or literal the `.s` writes.
- Entities against the source: every labelled list decoded from the ROM equals the
  `.s` macros of that label, field by field (37702 fields, 1152 lists).
- Bounds: data lengths match the layer sizes; every tile read stays inside BG
  VRAM; every palette a visible 16-colour layer uses was loaded; 256-colour layers
  have all 16 BG palettes.
- Exits: every area warp leads to a room that exists, from inside its room.
- Entities and tile entities inside their room, except a pinned list of data
  quirks (see below).
- Visual: rooms 0 and 1 of area 0x20 drawn from `maps.json` against a console
  screenshot (`/tmp/tmc-shot/top.png`, wide mode: room 0 at x 160, y 40, room 1
  left of it). The console applies a colour correction, so the test learns a
  per-channel colour mapping from the two rooms and checks that every pixel gets
  the mapped colour: room 0 matches on 90.9% of its pixels, room 1 on 98.7%. All
  the differences are sprites and HUD: Link, Gentari, the red curtain
  (`OBJECT GENTARI_CURTAIN` at 24,80, which hides a doorway in the BG), the two
  `MINISH_SIZED_ARCHWAY` glows, the item icons. Skipped without the file or PIL.
- Sprites (`sprites.json`): the object and NPC definition tables found in the ROM equal their `.inc` files entry by
  entry, forms included; the enemy and projectile tables (213 and 79 records, forms included) equal the C source in
  gfx, palette, flags, health, speed, damage, mask and (for the symbolic ones) sprite index; every OBJECT / ENEMY / NPC /
  PROJECTILE entity that is positioned has a sprite or a stated reason; pinned counts of default sprites per kind and of
  markers; the **catalog** (frames, animations, sources, tiles) is within bounds and re-renders every stored picture
  (2924) byte for byte from the JSON alone, which is the data the viewer's renderer works from; sprite pointer count, OBJ size
  table, `gUnk_0811F690` against the port / the source; every record names a
  positioned entity of its family; every drawn piece lies inside the gfx it reads
  (and OBJ VRAM) and inside the palette rows it uses, and re-rendering gives the
  stored picture; the source lines each rule relies on still exist; doors within
  48 px of a room edge face that edge (`type & 3`); a south boss door is the
  vertical mirror of the north one; the curtain of room `20-00` lies on pixels where the
  console screenshot differs from the layers (97.7% of its pixels).
- `test_depth_inc.py` (73 checks): the `.inc` parser and writer: header comment and entries inside comments are not
  entries, unknown lines are reported, INVERT and DOWN, any and exact types, attached comments, replace / remove / add and
  where, CRLF, the identity round trip on a sample and on the port's real file, three-way application of a change to a
  file that moved meanwhile, validation; and the two endpoints against temp copies (and that the real `.inc` and
  `sprite_view.json` are untouched).
- `test_zone_inc.py` (70 checks): the zone `.inc` parser and writer (hex and decimal numbers, header and block-comment entries,
  unmanaged lines, attached comments, in-place `toBg` change, remove, where an add goes, header-only, empty and missing files,
  CRLF, the identity round trip, a file changed meanwhile), the validation (unknown room, outside the room, `bg == toBg`, bad
  numbers, the undefined-layer warning, the first example against the real `maps.json`), and the endpoint against temp copies
  (creation with the header, edit, refusals, and that the real `port_zone_depth.inc` is not touched).
- Regression checksums of layers, entities, tile entities, exits, gfx, metatiles
  and the sprite records, pictures, catalog and swapped tiles. They say nothing changed by accident, not that it is right; on a
  deliberate change the test prints the new values to paste.

## What the data shows that one would not expect

- Positions outside the room, in the ROM's own data (pinned in the test): Lake
  Hylia's beanstalk room (1008x192) points its enemy list at the main room's
  octoroks; Festival Town (400x960) reuses Hyrule Town's callback and chests;
  the beanstalk tops place the `BEANSTALK` object below the bottom edge; a small
  chest at y 656 in a 432-high dig cave; `TORCH_TRAP` keeps a parameter in `y`;
  unused areas 0x45 and 0x67.
- Metatile indices past the area's loaded set: Dig Caves room 3 (one index,
  0x81D, past the 2048-entry table) and unused areas 0x77 and 0x87. The game
  reads stale memory there; the viewer should mark them.
- One entity of kind 5 (not in `EntityKind`) in Temple of Droplets room 49.
- The curtain in Gentari's room is `OBJECT GENTARI_CURTAIN` (0x58), not
  `ENEMY CURTAIN` (0x5E, `src/enemy/curtain.c`).

## Not covered

- **The right pose of a default sprite** (it is animation 0's first frame; the init code decides the real one),
  the sprites of the entities listed under "Left as markers", **animation** (only a still pose is drawn, the picker steps
  through frames by hand), **sprites with several parts** (`SetExtraSpriteFrame`, multi-part entities), **OAM priority** (sprites are drawn over every layer, sorted
  by y), shadows and anything tied to the player or runtime state: see "Sprites".
- **Runtime map changes**: `SetTile` from entities (opened chests, pushed blocks,
  special tiles 0x4000+, none of which are in the static maps), destructible
  tiles, dark rooms, palette effects, and BG3 and its scrolling from managers.
- **Conditions** of the callback lists, and lists loaded by functions the
  callbacks call, or by entities and scripts (delayed entities, kinstone fusions,
  cutscenes).
- **BG0** (text and HUD; no room data) and the gfx groups at VRAM 0xC000 and up
  (HUD, items), which BG1 and BG2 cannot reach.
- **Collision maps** and tile types beyond copying the type tables.
- **The USA ROM** is supported by the code (its offsets are in `port/port_rom.c`)
  but untested: the counts and checksums in `test_maps.py` are for EU only. The enemy/projectile table reader
  preprocesses the source for EU (`EU` defined, no `MULTI_REGION`); the USA branch is untried.
- **The zones' effect**: the window writes the file and draws an outline and a 2 px illustration; that the port honours the rectangle
  at the right plane is for the console to show. Zones are static: they do not follow runtime tile changes, and the illustration
  ignores sprites and layer priorities beyond the draw order.
- **The depth modes' effect**: the panel writes the file and draws an illustration; whether the renderer does what the
  mode says is for the console to show (3D WORLD on, FORCE 3D).
