# Extending a scene's picture for the 400x240 view

Some screens the GBA drew at 240x160 are one fixed picture: a cutscene's background, the title's
forest. In Full View (Wide + Pixel Perfect) the port shows them at 400x240, and the columns left
and right of the original picture do not exist in the game's data. The owner fills them with a
generative AI (Antigravity) from a screen dump; the game reads the result from the SD card.
Done for the Minish portal cutscene (PLAN P4.3); planned for the title screen (P4.4).

**What ships:** only the generated parts (the original area left transparent), in
`platform/3ds/assets/wide-scenes/` and, converted, in the romfs (`platform/3ds/romfs/wide-scenes/`,
owner's decision, 2026-10-13, so players have them). The dump, the template and the whole
picture (with the game's art in it) stay on the owner's PC (`~/Imágenes/<scene>/`). A file of the
same name in the data folder replaces the shipped one, for trying a new picture.

## 1. Dump the scene and extract its picture

With a debug build on the console and the scene on screen (a save state just before it helps):

```sh
tools/tmc-link/tmc-link.py dump                      # prints the dump number, e.g. 25
for f in vram.bin io-registers.bin palettes.bin oam.bin top-screen.bmp info.txt; do
  curl -s -O "ftp://$IP:5000/3ds/Minish%20Cap%203DS/debug/tmc-dump-0025-$f"; done
```

Fetch the files by name: listing `debug/` with a thousand files in it hung the FTP server.
`info.txt` and the dump's `-io-registers.bin` say which BG holds the picture (its `BGnCNT`: size,
colours, screen base); the game's source sets it up (`src/enterPortalSubtask.c`, `src/title.c`).

```sh
tools/scene-extend/extract_bg.py tmc-dump-0025 1 ~/Imágenes/<scene> --sprites
```

writes `bgN-full.png` (the whole BG map), `bgN-picture.png` (the rows with content), `bgN-template.png`
(the picture placed as the 400 view shows it, transparent around: `--left` columns before it,
default 80 for a 240 picture centred in 400) and `bgN-with-sprites.png` (the picture with the
dump's sprites, for context). Look at what moves: sprites and other BGs are drawn by the game
over the picture and must not be painted into it.

**Rooms that are a scrolling map** (houses, shrines, most interiors; `DISPCNT` shows the map layers, the
dump's `info.txt` has `Map BG controls`): their BGs hold a 32x32 window of the room, so `extract_bg.py` finds a
few tiles. Use `tools/scene-extend/extract_map_room.py` instead, which draws the room from the dump's
`-map-bottom-special.bin` and `-map-top-special.bin` (fetch them and `-room-controls.bin` too) and writes
`room-ground.png`, `room-top.png`, `room-picture.png` and `room-template.png`.

**Rooms too tall for one picture** (the Crenel paths' shaft, area 0x12 room 00, 240x800): the AI gets the room in
pieces, each continuing the one before. `tools/scene-extend/chunked_extend.py make PICTURE DIR N` writes
`DIR/chunk-N.png` (400x320 by default: the original at x 80, black beside it); from the second piece on, its first 80
rows beside the original come from the AI's answer for the piece before (`DIR/result-(N-1).png`), so the AI continues
it. `chunked_extend.py stitch PICTURE DIR` joins the answers into `DIR/extended.png`, each piece giving only its rows
past the overlap, the original pasted back over the middle. A layer with transparent pixels takes `--key ff00ff`: the
pieces show transparency and the areas to fill in pure magenta, which the AI keeps as the background, and `stitch` turns
it back into transparency. The shaft is three layers, each its own picture, as they scroll at different speeds
(`verticalMinishPathBackgroundManager.c`): the ground (BG2, 240x800), the rock ledges over it (BG1, 1.25 times the
ground's scroll, so 240x960 are seen) and the wall shadows (BG3, blended, 1.125 times). BG1 and BG3 are read from the
dump's `-map-top-special.bin` (BG3 from byte 0, BG1 from byte 0x2000; 32 entries a row, character base 2). The owner's
material is in `~/Imágenes/camino-crenel/` (`suelo/` and `capa-3d/`). The ground repeats every 320 rows after its
first 32, so only rows 0-351 are generated (a first piece and a bridge back to row 32); the game repeats them. The
ledges are two pieces repeated (one cut by the GBA screen's left edge, four times, the last 8 px further left; one by
its right edge, three times), so the AI completes each once and `tools/scene-extend/stamp_pieces.py` puts the
completed part beside every copy it finds in the layer (matched pixel for pixel, also moved sideways or cut at the
layer's end). The shadows need no
picture: BG3's map is BG1's, entry for entry (the same tiles, so the same shapes), with only the palette changed by
column, palettes 9-13 two tiles each from the wall inwards (mirrored on the right), every one a flat colour. The
extended shadows are the extended ledges' silhouette in the band colour of their distance from the wall, the darkest
past the room's edge.

## 2. The prompt for the AI

Give it `bgN-template.png` to fill, `bgN-picture.png` and `bgN-with-sprites.png` as references, and:

> This is a background from a Game Boy Advance game (pixel art, 1 pixel = 1 game pixel). Fill the
> transparent areas of the template so the picture continues naturally to the left and right.
> Rules:
> - Do not change a single pixel of the existing picture (x = 80 to 335 in the template): it is
>   the game's own art and will be drawn on top of yours.
> - Use only colours that already appear in the picture (it has a palette of N colours). No
>   anti-aliasing, gradients, blur, dithering the original does not use, or new hues.
> - Same pixel scale and style: hard pixel edges, the same outlines and shading steps.
> - Continue what reaches the edges (walls, trees, ground, light) so the seam at x = 80 and
>   x = 335 is invisible; add elements of the same kind (more of what is there), not new ones.
> - Do not repeat or mirror the existing picture, and leave out anything that is a sprite in
>   the reference with sprites (characters, objects): only the background.
> - Output: a PNG exactly W x H (the template's size), no transparency left.

For a screen shorter than 240 lines (the title: 160) the template has rows above and below too
(`--top 40 --bottom 40`) and the prompt says which sides to fill and the picture's rectangle;
`check_extension.py ... --top 40` then. Name what must not be painted (light rays, logo, text
the game draws over the background).

Ask for two or three variants; denser ones fitted better for the portal (the owner chose the one
with more mushroom shelves).

## 3. Check it

```sh
tools/scene-extend/check_extension.py ~/Imágenes/<scene>/extended.png ~/Imágenes/<scene>/bgN-picture.png
```

The original area must be unchanged (0 pixels; else paste the original back over it) and the new
parts should use only the original's colours (0 off-palette; else map them to the nearest).

## 4. Put it on the console, and in the game

```sh
tools/scene-extend/to_console.py SCENE extended.png --ip IP     # try it: into the data folder
```

To ship it: keep the generated parts (the original area transparent) in
`platform/3ds/assets/wide-scenes/` and convert that into the romfs:
`tools/scene-extend/to_console.py SCENE platform/3ds/assets/wide-scenes/NAME.png
platform/3ds/romfs/wide-scenes/FILE.bin`. The presenter draws only the parts outside the game's
own picture, scrolled with it, through the game's colour conversion and fades. The files are zlib streams
(2026-10-25: the seven shipped ones went from 1.2 MB to about 64 KB; zlib is the CI image's 3ds-zlib, required by the
build); `ScenePictures_ReadFile` reads that, the GBA BIOS's LZ77 (`to_console.py --lz77`, 2.7 times bigger, the way
out should zlib ever be missing) or the uncompressed picture (`--raw`, or an older player's file), told apart by the
size and the zlib header.

| Scene | File (romfs `wide-scenes/`, or the data folder) | `to_console.py` scene; pictures | Code |
|---|---|---|---|
| Minish portal cutscene (P4.3) | `portal-cutscene-strips.bin` (80 + 64 columns x 320) | `portal`; shipped: `portal-cutscene-1.png` (the owner's pick; `-2` the plainer variant) | `Port_Widescreen_CutsceneStrips`, `DrawScenePictures` (`platform_gpu_3ds.c`) |
| Minish portal cutscene, the rock | `portal-rock-strips.bin` (80 + 80 columns x 320) | `portal-rock`; shipped: `portal-rock-3.png` (the owner's pick, the cave walls; `-1` the plainer, `-2` with big crystals). The rock's picture (BG1) is 240x320, not 256: `extract_bg.py DUMP 1` says so. Each portal type loads its own picture (`src/enterPortalSubtask.c`), so the presenter picks the strips by the type (`Port_Widescreen_CutsceneStrips`, `sPortalStrips`); the other two types have none yet and get black | as the stump's |
| Crenel paths' shaft (area 0x12 room 00) | `crenel-paths-ground.bin` (160x352), `-ledges.bin`, `-shadows.bin` (160x960): the 80 columns each side of the room | `crenel-ground`, `crenel-ledges`, `crenel-shadows`; shipped: `crenel-paths-{ground,ledges,shadows}.png` (the shadows made from the ledges, below). Drawn by the GPU renderer as their BGs (ground's, BG1, BG3), each at its layer's scroll (`ScenePictures_SetShaftView` from `port_linked_stubs.c`; the ground's rows past 352 repeat from 32); the software renderer leaves the sides black | `build_bg_scroll_picture`, `PortPpuGpu3DS_SetScrollPicture`, `scene_pictures_3ds.c` |
| Minish Paths' grass corridor (area 0x11, the vertical one) | `minish-paths-leaves.bin`, `-shadows.bin` (160x1024): the 80 columns each side | `minish-leaves`, `minish-shadows`; shipped: `minish-paths-{leaves,shadows}.png`. The same manager as the Crenel shaft, so the same three layers and scrolls (`SCENE_SHAFT_MINISH` in `scene_pictures_3ds.h`); told apart by an FNV-1a hash of BG1's map (`gMapDataTopSpecial` bytes 0x2000-0x3FFF, 0xA9524361), as the strips fit only that map. The leaves were made by Gemini in four 320-row pieces (2026-10-25, `~/Imágenes/minish-paths/gemini-hojas/`), each fitted (scale about 5.77, colours mapped to the layer's 14 greens) and joined to the one before along the rows where both agree; the shadows from the extended leaves' silhouette in the darkest band colour, as Crenel's. The ground (240x800, repeating every 160 rows) ends in a band of its darkest brown, mostly under the leaves, so its sides are that colour, darkening in 16-column steps away from the room (16, 13, 10, 8 and 6 sixteenths: more plants, less light), made by the game (BG palette 14 colour 1, `groundFill`) and shown in plain WIDE too (owner, 2026-10-25) | as the Crenel shaft's |
| Minish Paths' water corridor (area 0x11 room 01) | `minish-water-ground.bin` (160x64, looped), `minish-water-leaves.bin`, `-shadows.bin` (160x1024) | `minish-water-ground`, `minish-water-leaves`, `minish-water-shadows`; shipped: `minish-water-{ground,leaves,shadows}.png`. Its ground's banks are reed stalks repeating every 64 rows (rows 64-736): Gemini continued three periods, the middle one became the tile, then the tile was given back with a join in the middle and a band to repaint, so the loop has no seam (`SCENE_SHAFT_MINISH_WATER`, ground loop 0-64). The leaves repeat every 512 rows: two 320-row pieces, the loop's join repainted the same way (a magenta band, as black means "no leaf" there); the shadows are the leaves' silhouette exactly, in the edge colour (24,24,80). The ends (rows 0-63 and 736-799, rocks and light water) are not continued: stalks beside them, mostly under the leaves (owner, 2026-10-25) | as the Crenel shaft's |
| Minish Paths' rooms 02-04 (area 0x11: to Hyrule Town, Lon Lon Ranch, the mayor's cabin) | `minish-town-leaves.bin` (02 and 03), `minish-cabin-leaves.bin`, `minish-{town,ranch,cabin}-shadows.bin` (160x1024); 04's ground is `minish-water-ground.bin` | the scenes of those names; shipped: the `.png` of each. One set of leaves for the three (`SCENE_SHAFT_MINISH_TOWN`, `_RANCH`, `_CABIN`): 03's BG1 is 02's but for 15 pixels of the room, 04's is 03's in bluer greens (each colour mapped to one), so Gemini made 02's sides in four pieces and 04's are them recoloured; 03 and 04 share BG1's hash and are told apart by room. The shadows: the leaves' silhouette in each room's edge colour, (64,48,32), (96,64,40), (24,24,80). Grounds: 02's and 03's their edge colour darkening outwards (plain WIDE too); 04's banks are the water corridor's stalks in the same phase, so it takes that loop | as the Crenel shaft's |
| Title screen (P4.4) | `title-forest.bin` (400x240) | `title`; shipped: `title-forest-a.png` (`-b` the framed variant). The forest (BG1, 240x160, 22 colours) needs all four sides (`extract_bg.py DUMP 1 --top 40 --bottom 40`). The title is drawn at 400x240 (`Port_Widescreen_TitleWide`, `src/title.c`): the picture *is* BG1 in both renderers (`virtuappu_mode1_bg1_picture`, the GPU's picture texture), so the light rays blend over it and the sword and sprites go over it; the rays are continued by the game itself (below) | `scene_pictures_3ds.c`, `PortPpuGpu3DS_SetBg1Picture` |
| File select (P4.5) | `fileselect-cave.bin` (400x240 palette indices) | `fileselect --palettes DUMP-palettes.bin`; shipped: `fileselect-cave-1.png` (the owner's pick, "monumental"; `-2`, `-3` the other variants). The cave (BG3, 240x160, 35 colours, BG palettes 11-14) needs all four sides (`extract_bg.py DUMP 3 --width 240 --height 160 --top 40 --bottom 40`); with the GPU renderer the screen is drawn at 400x240 (`Port_Widescreen_FileSelectWide`): BG0-BG3 in the centred 240x160 (`bgNativeMask`), the cave as a picture over BG3 (`PortPpuGpu3DS_SetIndexedPicture`), and the sprites moved into the middle by `port_draw.c`, each drawn thing as one, carried faster across the GBA screen's edge so it leaves the 400 view as it leaves the 240 one (`WideCarry`). With the software renderer the screen stays 240x160 and the presenter draws the cave around it. Its shimmer is the game cycling palettes 11-14, so the file holds indices, not colours (below) | `port_draw.c`, `PortPpuGpu3DS_SetIndexedPicture`; software: `CaveReady`, `DrawScenePictures` (`platform_gpu_3ds.c`) |

Screens whose background is one plain colour need no picture: the Nintendo/Capcom logos at the
start, and the blank frames between the title and file select, are drawn at 240x160 and the
presenter stretches the frame's edge columns and rows to the screen's edges (`DrawFrameEdges` in
`platform_gpu_3ds.c`, `Port_Widescreen_StretchEdges`), so the background and the game's fades fill
the 400x240 screen. A screen keeps its view while the game fades it out (the title task's state 2,
when `gUI.lastState` already names the next screen).

A new scene needs its view at 400x240 first (the scene's own layout: how the picture and the
sprites are centred, which BG repeats), then a strips file and its converter as for the portal.

## The vertical Minish paths: what each corridor needs (survey of 2026-10-25)

Every room drawn by `verticalMinishPathBackgroundManager.c` is 240x800 with the same three layers (the ground on the
room's own BG; BG1 = leaves or rock ledges, at 1.25 times the scroll; BG3 = their shadows, blended, at 1.125), so they
all go through the same code: one row each in `kShafts` (`platform/3ds/source/scene_pictures_3ds.c`), found by area and
room, BG1's strips guarded by the FNV-1a hash of BG1's map (bytes 0x2000-0x3FFF of `gMapDataTopSpecial`). The owner
decides each one in turn (2026-10-25): not every ground should be the same dark gradient.

| Room | What it is | BG1 hash | BG1 layer | Ground sides | State |
|---|---|---|---|---|---|
| 0x11/00 | grass corridor, cobbles and moss (the first one, after the Minish Woods) | `a9524361` | leaves, no repeat (1024 rows) | ground repeats every 160 rows; outer columns 100% (56,32,32), BG pal 14 colour 1 | done: Gemini leaves, derived shadows, edge brown darkening outwards |
| 0x11/01 | water: lily pads, giant flowers, vines; bottom exit to area 0x04 room 00 (swamp) | `46df39a1` | leaves and vines, repeat every 512 rows | reed stalks repeating every 64 rows (rows 64-736); ends are rocks and light water | done: Gemini stalks (64-row loop) and leaves (512-row loop); ends not continued |
| 0x11/02 | to Hyrule Town: giant flowers and bellflowers (sprites, #50), an Ezlo-sign block | `e0e89302` | leaves, the same as 03's but 15 pixels at rows 512-513 | outer columns 100% (56,32,32), BG pal 14 colour 1 (or 6/1) | done: Gemini leaves (shared with 03/04), derived shadows, edge brown darkening outwards |
| 0x11/03 | to Lon Lon Ranch: cobbles, rocks at the sides repeating every 64 rows, white spirals (sprites, #51) | `84b4d4e4` | 02's leaves, no repeat | outer columns 64% (192,160,88), BG pal 14 colour 5 | done: 02's leaves, derived shadows, ochre darkening outwards (on trial; Gemini rocks in a 64-row loop the alternative) |
| 0x11/04 | to the mayor's cabin: water, cattails (sprites, #52) | `84b4d4e4` | 03's leaves pixel for pixel in bluer greens (one colour for each) | the water corridor's reed stalks, row for row in the same phase (rows 64-736) | done: 02's leaves recoloured, derived shadows, the water corridor's stalk loop |
| 0x12/00 | Crenel paths' shaft | (by room) | rock ledges, two pieces repeated | Gemini, rows 32-351 repeating | done |
| 0x12/01 | Crenel paths' second vertical one | `a6dbee71` | rocks at rows 120-963, 62% alike at 512: repeated pieces (`stamp_pieces.py`) | outer columns 57% (112,64,48), BG pal 8 colour 5 | to do |
| 0x12/02 | Crenel, horizontal (800x192, BG1 0x1e09, `horizontalMinishPathBackgroundManager.c`) | - | - | - | not handled by this code |

Area 0x11 has rooms 0-4 (room 5 loads with size 0). The shadows checked so far (00, 01) are BG1's silhouette with a
flat colour per band, the darkest at the walls, so the extension is the extended leaves' silhouette in that colour
(01: exactly the same pixels; 00: 92% of them). Leaves 02/03/04 are not room 00's: their edge columns match it only in
stray rows, so they need their own pieces: one set for the three (2026-10-25, `~/Imágenes/minish-paths/gemini-hojas-02-04/`).
The giant flowers, bellflowers, white spirals and cattails beside rooms 02-04 are sprites (`HUGE_DECORATION`, object
0x78, `src/object/macroDecorations.c`) moving at 1.5 (or 1.125) times the scroll; their art stops at their sprite's
edge, the GBA screen's, so it shows cut in Full View (#50-#52; every frame drawn from the ROM by
`tools/layer-workbench/sprites.py`).

**Material** (the game's art: on the owner's PC only, never in git): `~/Imágenes/minish-paths/survey/<area>-<room>/`
(0x11-0 to 0x11-4, 0x12-1, 0x12-2) holds each corridor's dump (`tmc-dump-*`: map layers, VRAM, palettes, registers, the top screen), `room-ground.png`
(`extract_map_room.py`) and `bg1-leaves-full.png` / `bg3-shadows-full.png` (`extract_minish_path.py`).
`~/Imágenes/minish-paths/gemini-hojas/`, `gemini-hojas-agua/` and `gemini-tallos-agua/` hold the templates, the
prompts, Gemini's answers and the fitted results of the corridors done; their `PROMPT*.txt` are the texts to reuse.
The dumps were taken in Azahar with `tmc-link.py warp` (`docs/debug-tools.md`) on a test file.

## AI pieces: the workflow

`tools/scene-extend/ai_pieces.py` does every step done by hand for the corridors above:

```sh
L=~/Imágenes/minish-paths/survey/0x11-2/bg1-leaves-full.png          # the layer (only its first 240 columns are used)
tools/scene-extend/ai_pieces.py make $L 1 trozo-1.png                 # 400x320, the sides black: give it to the AI
tools/scene-extend/ai_pieces.py fit trozo-1.png answer-1.jpg result-1.png --palette $L
tools/scene-extend/ai_pieces.py make $L 2 trozo-2.png --prev result-1.png   # its first 80 rows of sides already painted
tools/scene-extend/ai_pieces.py fit trozo-2.png answer-2.jpg result-2.png --palette $L --keep 80
```

Pieces of 320 rows start every 240 (the last one moved up to end at the layer's end, so it keeps more rows: `make`
says how many; pass that to `fit --keep`). Stitch the results by **replacing** rows (a piece's rows from its start),
never by compositing them over each other, then paste the original's 240 columns back and check that it is unchanged
and no colour is outside the original's.

- **A layer that repeats** (the water corridor's leaves every 512 rows, its stalks every 64): give the AI one period
  and a bit, take one period, then make a picture of the period's end followed by its start (the join in the middle),
  the sides painted except a band around the join, and have the AI repaint the band; `fit --band Y0 Y1` keeps only
  the band between two seams. Mark the band in **magenta** on a layer with transparency (black there means "no leaf")
  and say so in the prompt. Ship one period; the game repeats a ground with `groundLoopStart`/`groundLoopLength`, and
  a repeating BG1 is written twice into its 1024 rows.
- **Fitting**: Gemini answers 5.6-7.4 times the size, often not the same in x and y, sometimes squeezed inside a margin
  (5.64 wide with 72 pixels each side, 2026-10-25); `fit` searches scales within 0.45 and offsets within 96, coarse
  then fine, and prints the mean difference on the original part (under 30 is good). The answer may change what it was
  told to keep (78% kept once): the seams take only where it agrees with the template.
- **Prompts**: say what the layer is (leaves over water, stalks under leaves...), the exact columns to paint, "pure
  black = nothing" on a transparent layer or "paint every black pixel" on a ground, how many colours, that it must
  repeat every N rows when it does, and what not to draw (water, flowers, characters).

## Patterns: extend them by code, no AI

**WIDE or WIDE + AI** (owner, 2026-10-25): WIDE + AI exists only because some players want nothing made by a
generative AI. An extension made by code from the game's own art (no generative AI anywhere) is shown in plain WIDE
too: its `ScenePicture` has `.procedural = true` (`platform_gpu_3ds.c`). Anything a generative AI drew, even in part,
stays behind WIDE + AI.

A background that repeats one motif on a lattice needs no AI and no file: the game extends it itself, from its own
VRAM, while it is shown, so nothing of the game's art is shipped (owner, 2026-10-25: code-made pictures are not kept
in git, nor written to the SD). The pause menu's (BG3: Ezlo caps on yellow, the same on every page, never scrolled)
is the first: `ScenePictures_PauseIndices` (`platform/3ds/source/scene_pictures_3ds.c`) reads BG3's 30x20 tile map,
finds the two shortest shifts that repeat it (3,6 and 13,10 tiles; the map is off the pattern in a few places), gives
every entry outside the screen the one most of its lattice copies inside have, and completes the caps the screen's
edge cuts off the lattice (one at the left edge, tile rows 13-18) from a whole copy found elsewhere in the map. It
takes a few milliseconds when the menu opens (again only if BG3's map changes), and the picture is palette indices,
so fades and colour correction apply as to the game's own tiles.

`tools/scene-extend/extend_lattice.py` does the same in pixels on a dump's picture (`extract_bg.py`), for trying a
pattern on the PC before writing it into the game; its result is for looking at, not for git.

With the GPU renderer the pause menu is drawn at 400x240 while it shows that background unscrolled
(`Port_Widescreen_PauseWide`, as the file select): its BGs in the middle, the picture as BG3 around them, and every
sprite moved 80,40 as it is, so what the game slides past the GBA's screen (the L and R buttons when pressed) shows
whole; a sprite wholly off it stays hidden. The title banner (INVENTORY, COLLECTION, MAP: two 64x32 sprites at the
screen's top-left, cut flat by the GBA's left edge) is closed by `Port_Draw_PauseBannerCap` (`port/port_draw.c`):
four 8x8 (or 16x8) sprites showing its right end's tile column flipped, after it in OAM. With the software renderer
the screen stays 240x160 and the presenter draws the picture around it (`Port_Widescreen_PauseBackgroundShown`).

## Layers animated by palette: continue them, do not paint them

A layer the game animates through its palette or its blend (the title's light rays, BG0: the
palette rotates every 8 frames, `UpdateLightRays`, and the blend changes every 32) cannot be a
painted picture: it has to stay palette indices in the game's own BG, so the animation goes on.
When the layer is made of straight parallel strokes, as the rays are, it is continued by copying
it, without an AI and without shipping anything:

```sh
tools/scene-extend/extend_rays.py ~/Imágenes/<scene>/tmc-dump-0026 --forest \
    platform/3ds/assets/wide-scenes/title-forest-a.png ~/Imágenes/<scene>/bg1-picture.png --forest-at 80,40
```

reads BG0 from the dump as indices, finds each ray (8-connected), checks that every one repeats one
row down and one column left (45 degrees; it stops and names any that does not), and continues each
diagonal the picture cuts (top row, last column) to the view's edge by repeating a stretch of the
ray's upper half on that diagonal; the original's pixels are not touched. `rays-preview.png` and
`rays-on-forest.png` (blended 9/9 over the forest) show the result. Gaps the GBA's screen never
showed get copies of whole rays: `--suggest` lists places where a copy keeps 8 pixels from every
ray and stays off the original's area; `--add RAY:DX,DY` draws one (rays are numbered as the script
prints them).

The game does the same itself (`platform/3ds/source/title_rays_3ds.c`, the placements in
`kNewRays`): on the title in Full View it computes the extension from the BG0 it loaded and writes
it into VRAM as new tiles (at the top of character block 0, past every tile BG0 and BG1 use) and a
512x256 map in screen blocks 26-27 (unused by the title), with the original's map entries at
+80,+40; `src/title.c` points BG0 at that map. Both renderers then draw it as the game's BG0. It is
rebuilt when the source changes and rewritten when something overwrites it. The added pixels are the
ROM's own art, so they are never in the repository or the romfs. Check a change with
`platform/3ds/tests/title_rays_3ds_test.c` (against a dump's `-vram.bin`; it writes the extended
layer as indices, to compare with the script's `rays-indices.png`).

### A generated picture over a palette-cycled background

The file select's cave shimmers because the game cycles its four palettes (11-14, every 16
frames), not because its pixels change. A generated extension painted in colours would stay still
beside it, so `to_console.py fileselect` turns the picture back into palette indices with the
palettes of a dump of that screen (`--palettes`): every pixel takes a palette that holds its exact
colour, the one most of its 8x8 block uses when it can, so a block shimmers as one (the generated
picture may only use the original's colours: `check_extension.py` says so). The presenter keeps
the indices and rebuilds its texture from the palettes the shown frame was drawn from
(`FadeGpu3DS_ShownPalettes`) whenever they change, so the extension shimmers on the same frames
as the original; the game's fades veil it as they do the title's forest. The indices are
generated art only, so they ship like the other extensions.

