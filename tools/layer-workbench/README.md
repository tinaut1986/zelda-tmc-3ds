# Layer workbench: room data extractor

Stage 1 of a desktop layer workbench for The Minish Cap, like the ones in
`../mzm/tools/layer-workbench` and `../sm-3ds/tools/layer-workbench`: the data
extractor and its tests. Stage 2 is the read-only HTML viewer (`serve.py` + `index.html`, see "Viewer").
Stage 2b adds sprites as pictures for a curated list of entity families
(`sprites.py` -> `sprites.json`, see "Sprites").

```sh
export TMC_ROM=/path/to/the-minish-cap.gba    # USA (BZME) or EU (BZMP)
python3 tools/layer-workbench/build_maps.py   # writes maps.json, maps.json.gz and sprites.json (~5 s)
python3 tools/layer-workbench/test_maps.py    # ~2.2 million checks (~5 s)
python3 tools/layer-workbench/render_room.py 0x20 0 /tmp/room.png [--entities]
```

`--rom PATH` works in place of `TMC_ROM`. Python 3 standard library only;
`render_room.py` and the visual test also use PIL when it is installed.

**`maps.json` and `sprites.json` are the game's art** (tiles, palettes, maps, sprite
pictures), and so is any PNG rendered from them. They are gitignored here; never commit, package or share them.
The repository's top `.gitignore` hides `*.py`; this folder's `.gitignore`
un-hides its scripts, so `git add tools/layer-workbench` picks them up.

## Files

| File | What |
|---|---|
| `tmcdata.py` | Readers: the ROM (pointer tables, LZ77, map definitions, palette groups), the enums in `include/`, the label layout of `data/map/entity_headers.s`, the entity loads in `src/roomInit.c`. |
| `build_maps.py` | Walks every area and room and writes `maps.json` (and, through `sprites.py`, `sprites.json`; `--no-sprites` skips it). |
| `sprites.py` | The OBJ side: object definitions, sprite/animation/frame tables, OAM pieces, tiles and palettes read from the ROM, plus the per-family rules (which animation/frame/flip the entity's init code picks) that give each entity a picture. |
| `render_room.py` | Draws one room from `maps.json` as the BG hardware would. Also the reference for the viewer. |
| `test_maps.py` | The checks (below). |
| `serve.py`, `index.html` | The desktop viewer (see "Viewer"). |

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
```

Serves this folder on `127.0.0.1:8765` and opens the browser. `maps.json` (and
`sprites.json`) are built first (`TMC_ROM` or `--rom`) when missing or older than
`build_maps.py` / `tmcdata.py` / `sprites.py`; a lone `maps.json.gz` is accepted too,
and the viewer works without `sprites.json` (markers only). Standard library only, and one
self-contained `index.html` (no CDN). Opened from `file://` the page cannot read
`maps.json`: choose or drop it (or the `.gz`) on the prompt it shows.

`index.html?room=AA-RR` (hex, the console's area/room) opens a room directly; the
address follows the room you open. `index.html?test` runs the self-checks: counts,
and the composite of 14 rooms (all layers, flips, palettes, special mode) hashed
against `render_room.py`'s output, the pictures of `sprites.json` (hashes in
`EXPECT_SPR`), that RESULT draws a sprite when SPRITES is on, and that a hidden layer's
window starts minimised; the result shows in a box and as the page title
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
| RESULT | The room composed, with the curated sprites over it (SPRITES); a checkbox per layer (BG1/BG2/BG3). Layers the game hides in that room are marked and need "show layers the game hides". |
| BG2 / BG1 / BG3 LAYER | One layer alone over a checkerboard. BG3's control is set by managers at runtime, so it has a char-base selector (a guess to try). A layer the game does not show in the room (`visible: false`, e.g. BG1 of the Minish houses) starts **minimised** (RELAYOUT and room changes keep that rule; it reopens in the next room that shows it) and, when opened, is drawn dimmed under a "NOT SHOWN BY THE GAME IN THIS ROOM, data may not be a drawable map" notice. RESULT keeps ignoring such layers unless "show layers the game hides" is ticked. |
| MAP | The rooms of the current area at their world positions; click opens; the current room is outlined; visited rooms show their picture. |
| ENTITIES | Markers switch, SPRITES switch, text filter, one checkbox per kind (ENEMY, OBJECT, NPC, MANAGER, ..., plus TILE for tile entities and EXIT), and the list. Click selects (Ctrl adds), double click centres the views on it. |
| INFO | The selection: for an entity with a sprite record, the picture and **why it is drawn that way** (the rule and the repo function it comes from, animation/frame, flips, offset, tiles, palette, and the runtime state the pose assumes); then every field of the objects, the metatile index, type and the four tile entries of each block (with a preview). COPY INFO and CLEAR SELECTION. |

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

Panning and zooming the image windows is synchronised (SYNC PAN, on by default); the
MAP window is independent. Metatile indices past the loaded set are flagged in the top
bar and in INFO. Read-only: editing and saving are a later stage.

## Sprites

Entities are positions in `maps.json`; what they look like is decided by their own
init code. `sprites.py` covers a curated list of families where that is certain and
writes `sprites.json` (`tmc-layer-workbench-sprites`):

```js
{ "pictures": { "<hash>": { "w": 32, "h": 64, "ox": -14, "oy": -32, "rgba": "<base64 w*h*4>" } },
  "rooms": { "20-00": { "1": { "family": "OBJECT/GENTARI_CURTAIN", "pic": "<hash>",
        "rule": "GentariCurtain_Init (src/object/gentariCurtain.c)", "sprite": 134,
        "anim": 0, "frame": 0, "flipX": 0, "flipY": 0, "dx": 2, "dy": 0,
        "gfx": "swapped, frame 0 of sprite 134", "palette": 132,
        "why": ["x += 2 (GentariCurtain_Init)", "..."], "state": "shut pose; ..." } } },
  "notCovered": { "OBJECT/SPECIAL_CHEST": "reason" }, "stats": {}, "skips": {} }
```

Room keys are the viewer's (`AA-RR`, hex); the inner key is the entity's index in the
room's `entities`. A picture is pre-rendered RGBA (the OAM pieces of the frame, tiles
and palette resolved, flips applied) whose top-left is (`ox`, `oy`) from the entity's
`x`, `y`. A record with `skipped` instead of `pic` says why it has none.

How an object is drawn, and where `sprites.py` reads each step (the ROM, the way the
port reads it, never the decomp's compiled USA tables):

| Step | Source |
|---|---|
| sprite index, OBJ palette id, gfx index and type | `gObjectDefinitions[id]` (`LoadObjectSprite`, `src/objectUtils.c`): 8 bytes per id, not in `RomOffsets`, so found by content in the ROM (the 4 plain records after id 2) and compared entry by entry with `src/objectDefinitions_data.inc` (EU pass) by the tests |
| animation / frame / flips / offset | the family's init code (`RULES` in `sprites.py`; each rule cites its function and the tests check its lines still exist) |
| first frame of an animation | `InitializeAnimation` / `FrameZero` (`port/port_animation.c`): `gSpritePtrs[sprite].animations[anim]`, 4-byte frames; the frame's spriteSettings flip bits XOR the entity's |
| OAM pieces | `gFrameObjLists[sprite]` -> frame table -> `{count, 5 bytes per piece}` (`LookupFrameData`, `RenderSpritePieces`, `port/port_draw.c`); the OBJ size table (`overlaySizeTable`) gives anchors, which is how a flip mirrors a piece |
| tiles | type 0: `gFixedTypeGfxData[gfx]` (`LoadFixedGFX`, `sub_080ADDD8`); type 1: the frame's tiles from `gSpritePtrs[sprite]` (`UpdateSpriteGfxSlot`); type 2 (already in VRAM) is not modelled |
| palette | `LoadObjPalette` / `LoadObjPaletteAtIndex` (`src/color.c`): ids 0-5 are the room's OBJ rows 0-5, 6-0x14 BG palette id-6 of the room, 0x16+ `objPalettes` in the ROM; a piece's palette addend selects the row |

Families drawn (EU ROM, rest pose; see each record's `state` for what it assumes):
`BOSS_DOOR` (animation `type & 0x1f`), `LOCKED_DOOR` (`frameIndex = type & 3`, `|= 4`
for key locks; gfx by area from `gUnk_0811F690`, read from the ROM), `MINECART_DOOR`
(same gfx; `frameIndex = type`), `METAL_DOOR` (`flipY`), `GENTARI_CURTAIN` (animation 0,
x + 2), `CHEST_SPAWNER` types 2-5 (animation 0), `LIGHT_DOOR` type 0, `TRAPDOOR`
(animation 0). Seen and left as markers: `SPECIAL_CHEST` (no sprite in its definition),
`CHEST_SPAWNER` types 0, 1, 6, 7 (a BG tile set at runtime), `HOUSE_DOOR_EXT/INT`
(spawners driven by the room's property table), `ROTATING_TRAPDOOR` (transient).
Small and big chests (`SMALL_CHEST`, `BIG_CHEST` tile entities) are BG tiles already in
the maps, not sprites. A wrong picture is worse than a marker: anything a rule cannot
resolve (a palette that is runtime state, a position outside the room, gfx already in
VRAM) is skipped with the reason.

Adding a family: write a rule (cite the init function), add the regexes it relies on
to `RULE_SOURCES`, run `build_maps.py`, `test_maps.py`, and paste the new counts and
checksums after looking at the rooms.

## Decisions

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
- Sprites (`sprites.json`): the object definition table found in the ROM equals
  `src/objectDefinitions_data.inc` entry by entry; sprite pointer count, OBJ size
  table, `gUnk_0811F690` against the port / the source; every record names a
  positioned entity of its family; every drawn piece lies inside the gfx it reads
  (and OBJ VRAM) and inside the palette rows it uses, and re-rendering gives the
  stored picture; the source lines each rule relies on still exist; doors within
  48 px of a room edge face that edge (`type & 3`); a south boss door is the
  vertical mirror of the north one; the curtain of room `20-00` lies on pixels where the
  console screenshot differs from the layers (97.7% of its pixels).
- Regression checksums of layers, entities, tile entities, exits, gfx, metatiles
  and the sprite records and pictures. They say nothing changed by accident, not that it is right; on a
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

- **Sprites of other families** (enemies, NPCs, most objects), **animation** (only a
  still pose is drawn), **OAM priority** (sprites are drawn over every layer, sorted
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
  but untested: the counts and checksums in `test_maps.py` are for EU only.
