# Colour grade

How the game's 15-bit palettes are turned into display colours, and where the
player can change that.

## What it is

The COLOURS option used to be a binary toggle. It is now a three-mode grade:

- **OFF** — the raw expansion the engine always did: each 5-bit channel becomes
  `x << 3`. Bit-exact with the old OFF.
- **GBA** — the fixed GBA LCD response curve (`kGbaLut`, 32 entries per
  channel). Bit-exact with the old ON.
- **CUSTOM** — the same 32-entry curve shaped by four parameters:
  - BRIGHTNESS, -50..+50 in steps of 10, added as `brightness / 100`;
  - CONTRAST, -50..+50 in steps of 10, scaling around mid-grey;
  - SATURATION, -50..+50 in steps of 10, a luma-preserving mix;
  - GAMMA, 0.4..1.6 in steps of 0.1, shown as `1.3` and stored as -6..+6.

Saturation also applies on top of the GBA mode: the fixed GBA words can be made
more or less colourful without leaving GBA. Brightness, contrast and gamma are
part of the CUSTOM curve only, so they do nothing while the mode is GBA or OFF.
The saturation controls are still editable in every mode; they take effect as
soon as a mode other than OFF is selected.

## Where it is adjusted

OPTIONS -> COLOURS on the bottom screen. The list cell shows the current mode
name and opens a page with five rows: MODE, BRIGHTNESS, CONTRAST, SATURATION,
GAMMA. Tapping a row cycles it; MODE goes OFF -> GBA -> CUSTOM -> OFF. The page
also paints a PREVIEW strip of eight fixed in-game RGB555 colours through the
installed grade, so a tap shows its effect at once.

The new words (MODE, BRIGHTNESS, CONTRAST, SATURATION, GAMMA, GBA, CUSTOM) go
through the existing text table (EN/FR/DE/ES/IT).

## Config keys

The grade is persisted in every backend:

- 3DS ini, `platform/3ds/source/port_config_3ds.c`;
- PC/Android `config.json`, `port/port_runtime_config.cpp`;
- `tools/ui-preview/stubs.c`.

| Key | Values |
| --- | --- |
| `colour_mode` | 0 OFF, 1 GBA, 2 CUSTOM |
| `colour_brightness` | -50..+50 |
| `colour_contrast` | -50..+50 |
| `colour_saturation` | -50..+50 |
| `colour_gamma` | -6..+6, one tenth per step (0.4..1.6) |

Out-of-range values are clamped by the parser and again by the module; an
unknown mode reads as OFF.

**Migration.** A file that only has the old `color_correction=1` and no
`colour_mode` loads as GBA; `color_correction=0`, or a missing key, loads as
OFF. When `colour_mode` is present it wins. Builds still write
`color_correction` alongside the new keys, derived as "mode is not OFF", so an
older build reading the same file stays coherent.

## How it is applied

The math and the installed state live in `port/port_color_grade.c/.h`, a
platform-neutral module. `Port_ColourGrade_Set` clamps the grade, rebuilds a
32-entry per-channel LUT and bumps `Port_ColourGrade_Epoch()` only when
something changed. The LUT is the only place with floating point; per-pixel and
per-entry paths are integer lookups. Saturation is an 8.8 fixed-point luma mix
(`luma = (299r + 587g + 114b) / 1000`), skipped when it is zero.

Each site keeps a copy of the LUT and checks the epoch instead of caching a
boolean:

- **Software renderer** (`port/ppu/src/mode1.c`): palette conversion goes
  through a private 32-entry copy of the channel LUT, rebuilt when the epoch
  moves; the BG/OBJ palette LUTs and the 4bpp pair tables are rebuilt on the
  same change.
- **3DS GPU renderer** (`platform/3ds/source/port_ppu_gpu_3ds_model.c`):
  `PpuGpu3DS_PackRgba5551` converts each palette word through the LUT (plus the
  saturation mix) and packs back to 5551. `PpuGpu3DS_CacheBeginFrame` compares
  the epoch against the one it last saw and bumps every palette generation, so
  the tile cache and the view rings decode again after a grade change.
- **Second-screen art** (`port/port_second_screen_color.c`):
  `Port_SecondScreen_Rgb555` calls `Port_ColourGrade_ToRgb888`, and
  `Port_SecondScreen_ColorEpoch` returns the module epoch.
- **Scene picture caches** (`platform_gpu_3ds.c`, `scene_pictures_3ds.c`):
  each cached texture records the epoch it was made with; a mismatch remakes it.
- **Desktop present** (`port/port_ppu.cpp`): a 256-entry 8-bit LUT (OFF an
  identity, GBA the existing gamma-4 -> sRGB formula, CUSTOM the module curve)
  is applied to the finished frame, with the saturation mix in the same pass.
  The same pass runs over the GPU supersample buffer.

The software renderer's palette path applies the channel curve only. Saturation
reaches the main picture through the GPU renderer and the desktop post-process;
it is also applied to the bottom-screen art through `ToRgb888`.

## Porting checklist (EstebanPdN upstream)

`port/port_color_grade.c/.h` only uses `<stdint.h>` and `<stdbool.h>` and knows
nothing about the 3DS, SDL or this fork, so it can be copied as is.

1. Copy `port/port_color_grade.c` and `port/port_color_grade.h`.
2. Add the five `colour_*` keys to the upstream `port_config_3ds.c`. Upstream
   already persists `color_correction` in the same ini style, so do the same
   migration: an absent `colour_mode` derives from `color_correction` (true ->
   GBA, false -> OFF), and keep writing `color_correction` as "mode is not OFF".
3. Grep the upstream tree for `color_correction` and `GetColorCorrection` to
   find every place that still branches on the old boolean. Upstream has no
   `port/` layer like this fork's, so those sites are in its own
   `port_ppu_3ds.c` and decode code; replace each one with the module (channel
   LUT for palette conversion, `Port_ColourGrade_Active` for "is a grade on").
4. Keep the invariants: OFF must stay the raw `x << 3` expansion, GBA must stay
   the fixed `kGbaLut`, and any cache that stores converted colours must be
   invalidated when `Port_ColourGrade_Epoch()` changes.

Upstream code was not inspected while writing this; the steps above are the
sites to search for, not a claim about their exact contents.

## Test

`port/port_color_grade_test.c` (xmake target `colour_grade_test`) covers OFF and
GBA parity, neutral CUSTOM parity, clamping, grey preservation, LUT
monotonicity, the epoch and the mode names.
