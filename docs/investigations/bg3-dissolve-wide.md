# The enemy's dissolve in the castle garden is wrong in Wide (Full View)

**Status: fixed in `fix/bg3-native-bounds-gpu` (`bb8c7ab`), owner to confirm (2026-10-12).**
Written so a new session can pick it up without the chat. GitHub issue: #5.

### Resolution (last commit of the branch)

In native bounds BG3's rows are sampled at the view's y and HOFS is taken to what the game
would have written natively, `base + native_left` (here -72 + 80 = 8): effect lines (within 32 px
of the frame's base register) keep their sine on top of it, the static frames of the effect
(written as 0) get the same offset. Measured with the recorder, enemy centre x 99.4 and
silhouette 99.5-99.8 (thumbnail px, 2 px each), GPU CHECK 0 pixels in 6 checks. The software PPU
(`mode1_render_text_bg_compact_tokens`) and the GPU builder (`build_bg3_native_bounds`) share the
rule. The wip experiment branch was folded into it. Still a heuristic tuned on this scene:
check the other BG3 HDMA effects (light rays, steam) before merging into the release.

To install a build from a branch that does not contain `release/v0.1.1`'s tip, set
`TMC3DS_VERSION=0.1.1-dev.xxx`: otherwise `version.sh` prints `0.0.0-dev+hash`, the CIA's title
version is older than the installed one and AM refuses it (`install write failed (d8e08027)`).

## Symptom (owner's words)

Scene: Hyrule Castle garden (area 07, room 00), a dialogue with Vaati's shadow; in the
owner's save state number 1 ("just as it fades").

- **Original view (240x160):** the enemy standing in front of the chest turns black and
  disappears right where it is, in front of the chest. The dissolve (a black silhouette that
  alternates between two positions and is then torn into horizontal slices) happens there.
- **Wide (Full View, 400x240):** the enemy is in front of the chest, then suddenly appears in
  the middle of the screen and the animation starts there, not in front of the chest.

Related, already fixed (see "Branches"): a stray column of pixels on the sword lying on the
floor, only in Wide (`fix/affine-obj-uv-inset`).

## How to reproduce on the console

Console: Old 3DS / 2DS at `192.168.1.139` (the game's own FTP server must be on:
DEBUG -> FTP SERVER; `.3ds_ftp_ip` in the repo). Load the owner's state and record:

```sh
L="python3 tools/tmc-link/tmc-link.py"
$L states                      # the owner's state is number 1 (07/00); delete any extra you create
$L loadstate 1
$L frames 36                   # wait N logic frames (see FRAMES below), then:
$L rec now                     # records the next 36 frames of the top screen (200x120 thumbnails)
$L frames 50
$L recfetch /tmp/rec           # /tmp/rec/frames.png contact sheet, frames.bin, per-frame list
```

Things that bite:

- **OPTIONS -> FRAMES must be "SIN SALTOS" (no skip).** With AUTO the Old 3DS skips
  presentations and the recorder (which samples *presented* frames) aliases the 1-frame-in-2
  flicker of the effect. Recordings are only comparable with no skip.
- **VISTA** (OPTIONS tab, top-left cell) toggles ORIGINAL <-> ANCHO (Wide Full View). The bottom
  screen returns to the MAP tab after `loadstate`; tap the options tab (153,12) *then* check a
  `shot` before tapping VISTA (40,38). A tap at (40,38) on the STATES tab is "+ NUEVO" and
  saves a new state (it happened; `delstate 1` removed it).
- `rec now` starts at the next present; the effect starts ~25 (original) / ~10 (wide) presented
  frames after the load and lasts ~100, so record windows (`frames K` before `rec now`, K = 0,
  36, 76 ...).
- Installing while the game runs crashed the console twice (Luma dumps: one `loader`, one kernel
  data abort; issue #4). Installs were done with FRAMES = SIN SALTOS and did not crash after that.
- `gpucheck` (GPU CHECK) compares the GPU frame with the software PPU's *in the same mode*; it
  cannot tell a bug both share. Its sets are `debug/tmc-check-NNNN-*` (CPU/GPU surfaces as
  RGBA5551 512x256 raw).

## What the effect is

`src/manager/vaatiAppearingManager.c`: `sub_0805D9D8` sets
`gScreen.bg3.xOffset = 0x80 - (vaati.x - gRoomControls.scroll_x)` and
`yOffset = 0x8c - (vaati.y - scroll_y)`; `sub_0805DA08` fills a per-line table
`pa = ((gSineTable[...] * x) >> 8) + gScreen.bg3.xOffset` and arms an HBlank DMA into
`BG3HOFS` ("BG3HOFS per-HBlank DMA armed every frame"). So the silhouette is **BG3**, drawn
at the screen position of Vaati (an entity), torn by a per-scanline horizontal offset. In
every other frame the table is all zeros (a "static" frame: the black blob), in the others it
is the register base plus a sine (the "smear").

## Findings

1. **BG3 under HDMA is "native bounds" in Full View.** `port/ppu/src/mode1.c`
   (`mode1_bg3_native_bounds_active`): when `BG3HOFS`/`BG3VOFS` are driven by HDMA and the
   view is bigger than 240x160, the software PPU draws BG3 only in the 240x160 canvas centred
   in the view (left 80, top 40 at 400x240) and samples source line = line - 40. The flag
   `virtuappu_mode1_bg3_hdma_native_bounds` is set in `platform/3ds/source/port_ppu_3ds.c`.
2. **The GPU builder did not know the flag** (PLAN already said "BG3 native bounds ... not
   checked"): it drew BG3 at the view's origin, silhouette 80 px left and 40 px up of the
   software PPU's (images 05/06, a GPU CHECK of the same frame). Fixed in
   `fix/bg3-native-bounds-gpu` (`build_bg3_native_bounds` in
   `port_ppu_gpu_3ds_model.c`): the layer is built as a native-sized frame and moved to the
   canvas; GPU CHECK went from 753 to ~97 px (the usual baseline).
3. **GPU == software PPU does not mean right.** After (2) both agree, and both still differ from
   the original view, because the *values* differ:
   - Original view: effect lines have `HOFS` ~ 0..16 (0 + sine), static frames 0.
   - Wide: the register base and the effect lines are ~440 (-72); static frames' lines are 0.
     `gRoomControls.scroll_x` was read while moving from 1805 (the wide camera's rest) to
     1872 (native rest) over ~20 frames, while the effect's offset was computed at the start
     and kept. The base register at frame start was 440 in every frame of the effect,
     including the static ones (the DMA table was 0 there).
4. **x compensation** (commit `e7731dc`): in native bounds, lines within 32 px of the frame's
   base register (`virtuappu_mode1_bg3_hofs_base`, captured in `port_ppu_3ds.c` before any
   HDMA step) are sampled with the base taken off. Effect and static frames then both sit
   under the chest horizontally (image 03), GPU CHECK ~0. This is a heuristic, not a
   derivation.
5. **The vertical position is still wrong.** In the same recordings the silhouette sits ~40 px
   lower relative to the chest than in the original view (60 px below it in Wide, ~20 px in the
   original: thumbnails of images 01 and 03). Sampling the canvas rows at the view's y
   (`line` instead of `line - native_top`, +40 on VOFS in the GPU) moves it up by exactly 40
   and looks like the original (image 04), **but** then GPU CHECK differs by ~660 px between
   the GPU builder and the software PPU (they disagree about something in that rule), so it is
   only in the `wip/bg3-vertical-experiment` branch, not a fix.
6. The "enemy" in front of the chest before the transformation is the purple figure drawn by
   OBJ/BG0..2 (Link-like, the shadow of Vaati's form); only the black silhouette is BG3. The
   owner's description ("appears in the middle of the screen") matches the silhouette being
   drawn at the wrong place relative to it: horizontally fixed by (4), vertically not.

## Images

All are contact sheets of 36 consecutive presented frames, 200x120 thumbnails (the original
view's frames are 240x160 scaled), read left to right, top to bottom.

| | |
|---|---|
| `img/01-original-view-reference.png` | Original view, from state 1 (frames 0-35). Reference: the figure under the chest turns black in place. |
| `img/02-wide-before-any-fix.png` | Wide, same range, before any change: silhouette low, centre/right. |
| `img/03-wide-after-x-compensation.png` | Wide, after the x compensation (window starting 76 frames after load): centred horizontally, ~40 px too low. |
| `img/04-wide-experiment-vertical.png` | Wide with the vertical experiment: where the original has it. |
| `img/05-parity-cpu-native-bounds-flag.png`, `img/06-parity-gpu-...png` | GPU CHECK of one frame before fix (2): CPU reference (05) vs GPU (06), silhouette 80 px left and 40 px up. |

## Branches (state when written)

In `release/v0.1.1`: the GPU fade, debug changes, UI language, `fix/fullscreen-quad-corners`,
`feat/rec-fade-trigger`.

Not in the release:

- `fix/affine-obj-uv-inset` (`115fb39`): sword sprite, owner-checked ("the sprite is clean").
- `fix/bg3-native-bounds-gpu` (`6285a45` canvas, `e7731dc` x compensation): partial, see above.
  Depends on the previous one (same file).

## Hypotheses and next steps

- The vertical rule must be derived like the horizontal one: the effect's `VOFS` base is
  `0x8c - (vaati.y - scroll_y)` with `scroll_y` the wide camera's. Check peek of
  `gRoomControls.scroll_y` and the per-line `BG3VOFS` in original vs Wide (the
  `io-per-line.bin` of a `dump`: `BG3HOFS` at 0x1c, `BG3VOFS` at 0x1e, 0x400 bytes per row).
  In the dumps taken so far `BG3VOFS` was 20 in every line (Wide) and 0 when the effect ended.
- Understand why the GPU builder and `mode1_render_text_bg_compact_tokens` disagree once the
  rows are sampled at `line`: compare tile rows (`sourceY & 7`), the band's `firstLine` and the
  Y translation in `build_bg3_native_bounds`.
- Better than heuristics: make the game side consistent. The camera moving from the wide rest
  to the native rest *during* the cutscene is the root of the 72 px; the port could hold the
  camera, or the effect could be computed at the settled camera. That is `src/`-side or
  `port_widescreen`, and a `src/` edit invalidates the save states (docs/save-states.md).
- Check the other BG3 HDMA effects (light rays `lightRayManager`, steam, the pause-menu
  `BG3CNT`) against both rules; the x compensation was tuned on this one scene only.
- Re-test with PIXEL PERFECT and the other styles the owner mentioned (he changes ESTILO and
  VISTA by hand: do not be surprised by changed options).

## Useful snippets

Per-line `BG3HOFS` of a dump (set N in `debug/`):

```python
import struct
b = open('tmc-dump-00NN-io-per-line.bin', 'rb').read()
hofs = [struct.unpack_from('<H', b, r * 0x400 + 0x1c)[0] & 0x1ff for r in range(len(b) // 0x400)]
```

Live registers: `tmc-link.py peek <gIoMem address + 0x1c> 4` (address from
`arm-none-eabi-nm build-3ds/game/tmc-3ds.elf`), `peek virtuappu_mode1_bg3_hdma_native_bounds 1`,
`peek gRoomControls 56` (`scroll_x` at 0x0a, `scroll_y` at 0x0c, `camera_target` at 0x30).

The frame recorder's contact sheet lists `BG3-NATIVE` for frames where the flag was on, and
`fade W/L` for the GPU fade (`docs/debug-tools.md`).
