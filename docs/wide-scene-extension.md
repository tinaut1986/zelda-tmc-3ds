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
own picture, scrolled with it, through the game's colour conversion and fades.

| Scene | File (romfs `wide-scenes/`, or the data folder) | `to_console.py` scene; pictures | Code |
|---|---|---|---|
| Minish portal cutscene (P4.3) | `portal-cutscene-strips.bin` (80 + 64 columns x 320) | `portal`; shipped: `portal-cutscene-1.png` (the owner's pick; `-2` the plainer variant) | `Port_Widescreen_CutsceneStrips`, `DrawScenePictures` (`platform_gpu_3ds.c`) |
| Title screen (P4.4) | `title-forest.bin` (400x240) | `title`; shipped: `title-forest-a.png` (`-b` the framed variant). The forest (BG1, 240x160, 22 colours) needs all four sides (`extract_bg.py DUMP 1 --top 40 --bottom 40`). The title is drawn at 400x240 (`Port_Widescreen_TitleWide`, `src/title.c`): the picture *is* BG1 in both renderers (`virtuappu_mode1_bg1_picture`, the GPU's picture texture), so the light rays blend over it and the sword and sprites go over it; the rays are continued by the game itself (below) | `scene_pictures_3ds.c`, `PortPpuGpu3DS_SetBg1Picture` |
| File select (P4.5) | `fileselect-cave.bin` (400x240 palette indices) | `fileselect --palettes DUMP-palettes.bin`; shipped: `fileselect-cave-1.png` (the owner's pick, "monumental"; `-2`, `-3` the other variants). The cave (BG3, 240x160, 35 colours, BG palettes 11-14) needs all four sides (`extract_bg.py DUMP 3 --width 240 --height 160 --top 40 --bottom 40`); the screen stays 240x160 in the middle and the presenter draws the rest. Its shimmer is the game cycling palettes 11-14, so the file holds indices, not colours (below) | `CaveReady`, `DrawScenePictures` (`platform_gpu_3ds.c`), `Port_Widescreen_FileSelectCaveShown` |

Screens whose background is one plain colour need no picture: the Nintendo/Capcom logos at the
start, and the blank frames between the title and file select, are drawn at 240x160 and the
presenter stretches the frame's edge columns and rows to the screen's edges (`DrawFrameEdges` in
`platform_gpu_3ds.c`, `Port_Widescreen_StretchEdges`), so the background and the game's fades fill
the 400x240 screen. A screen keeps its view while the game fades it out (the title task's state 2,
when `gUI.lastState` already names the next screen).

A new scene needs its view at 400x240 first (the scene's own layout: how the picture and the
sprites are centred, which BG repeats), then a strips file and its converter as for the portal.

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

