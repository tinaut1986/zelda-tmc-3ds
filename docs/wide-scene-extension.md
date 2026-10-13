# Extending a scene's picture for the 400x240 view

Some screens the GBA drew at 240x160 are one fixed picture: a cutscene's background, the title's
forest. In Full View (Wide + Pixel Perfect) the port shows them at 400x240, and the columns left
and right of the original picture do not exist in the game's data. The owner fills them with a
generative AI (Antigravity) from a screen dump; the game reads the result from the SD card.
Done for the Minish portal cutscene (PLAN P4.3); planned for the title screen (P4.4).

**Legal:** the dump, the template and the AI's picture are derived from the game's art. They stay
on the owner's PC (`~/Imágenes/<scene>/`) and the SD card, never in the repository, a release or
the CIA. The game works without them (black sides).

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

## 4. Put it on the console

Each scene has its own file in the data folder and its own converter; the presenter draws only
the new columns, scrolled with the picture, through the game's colour conversion and fades.

| Scene | File on the SD card | Converter | Code |
|---|---|---|---|
| Minish portal cutscene (P4.3) | `portal-cutscene-strips.bin` (80 + 64 columns x 320, RGB565) | `tools/portal-strips/make_strips.py extended.png --ip IP` | `Port_Widescreen_CutsceneStrips`, `DrawCutsceneStrips` (`platform_gpu_3ds.c`) |
| Title screen (P4.4) | not made yet: the forest (BG1, 240x160, 22 colours) needs all four sides, 80 left and right and 40 above and below (`extract_bg.py DUMP 1 --top 40 --bottom 40`); the light rays (BG0, blended over it) and the logo and sword stay the game's | | |

A new scene needs its view at 400x240 first (the scene's own layout: how the picture and the
sprites are centred, which BG repeats), then a strips file and its converter as for the portal.
