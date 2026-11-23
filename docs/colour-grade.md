# Colour grade

How the game's 15-bit palettes are turned into display colours, and where the
player can change that.

## What it is

The COLOURS option used to be a binary toggle. It is now a four-mode grade:

- **OFF** — the raw expansion the engine always did: each 5-bit channel becomes
  `x << 3`. Bit-exact with the old OFF.
- **GBA** — the fixed GBA LCD response curve (`kGbaLut`, 32 entries per
  channel). Bit-exact with the old ON.
- **CUSTOM** — the same 32-entry curve shaped by six parameters:
  - BRIGHTNESS, -50..+50 in steps of 10, added as `brightness / 100`;
  - CONTRAST, -50..+50 in steps of 10, scaling around mid-grey;
  - SATURATION, -50..+50 in steps of 10, a luma-preserving mix;
  - GAMMA, -20..+20 in steps of 1, factor `1 + gamma * 0.05` (0.0..2.0 in
    steps of 0.05). It is displayed in hundredths as `100 + gamma * 5`, so
    0.6 is stored as -8 and 1.25 as +5;
  - SHADOWS, -50..+50, an additive per-channel lift weighted to the dark end;
  - MIDTONES, -50..+50, an additive lift peaked at 50% luma.
- **VIVID** — a fixed CUSTOM preset: brightness +10, contrast +10, saturation 0,
  gamma 0.6 (stored as -8) and neutral zones. It has no parameters of its own:
  the stored BRIGHTNESS/CONTRAST/SATURATION/GAMMA/SHADOWS/MIDTONES fields are
  kept untouched while VIVID is selected, so switching back to CUSTOM restores
  what the player had set. The other modes never see those fields.

Saturation also applies on top of the GBA mode: the fixed GBA words can be made
more or less colourful without leaving GBA. The SHADOWS and MIDTONES lifts also
work in GBA and CUSTOM; they do nothing in OFF or VIVID. Brightness, contrast and
gamma are part of the CUSTOM curve only, so they do nothing while the mode is GBA
or OFF. The saturation and zone controls are still editable in every mode; they
take effect as soon as a mode other than OFF is selected.

## Shadow and midtone zones

Both zones are additive on the three channels, indexed by the post-curve luma
`l` (0..255, `(299r + 587g + 114b) / 1000`). Each is a -50..+50 parameter, 0
neutral, edited from the COLOURS page like the other fields:

- shadow weight `wS = (128 - l) / 128` for `l < 128`, else 0: 1 at black,
  falling linearly to 0 at 50% luma. It used to fade quadratically to 0 at
  37.5% luma, which left the game's real shadows (post-curve luma 20..60)
  almost untouched; the linear ramp reaches them at full strength;
- midtone weight `wM = 4 * (l / 255) * (1 - l / 255)`: 1 at 50% luma;
- `adj(l) = (shadows / 100) * 255 * wS + (midtones / 100) * 255 * wM`,
  rounded to an integer. Every channel gets the same `adj(l)`, then clamps to
  0..255.

With SHADOWS +50 and neutral midtones the table reads:

| luma | 0 | 24 | 48 | 64 | 96 | 128 |
| --- | --- | --- | --- | --- | --- | --- |
| `adj(l)` | 127 | 104 | 80 | 64 | 32 | 0 |

The 256-entry table is rebuilt with the LUT when the grade changes and cached by
epoch. `Port_ColourGrade_ChannelLut` stays curve-only: a zone never changes an
isolated channel, only a colour's overall lift.

## Where it is adjusted

OPTIONS -> COLOURS on the bottom screen. The list cell shows the current mode
name and opens a page in the compact OPTIONS style: a MODE cell, a BACK cell
and six field rows — BRIGHTNESS, CONTRAST, SATURATION, GAMMA, SHADOWS,
MIDTONES — each as name, value and a `-` / `+` button. A button steps the field
by one (gamma by 0.05, shown as "1.25") and clamps at its limit; nothing wraps.
GAMMA reads in hundredths (`100 + gamma * 5`), so -8 is "0.6" and +20 is "2.0";
the two zones read as signed points ("+10", "0"). The page scrolls by dragging
when the rows and the PREVIEW strip reach past the content area.

Tapping the MODE cell opens a floating popup instead of cycling in place: four
rows OFF / GBA / VIVID / CUSTOM, the current one outlined, each with four
mini-swatches of the preset's look; a tap on a row applies it and leaves the
popup open, so presets can be compared, and the X in the header or a tap
outside the box closes it without changing anything. The swatches are cached
and remade when the grade, the applied mode or the popup's open state changes.
The page also paints a PREVIEW strip of eight fixed in-game RGB555 colours
through the installed grade, so a step shows its effect at once.

Each `-` / `+` button steps once on the tap and repeats while the finger is
held on it (about 400 ms before the first repeat, then every 100 ms); sliding
off the button, lifting or scrolling stops it. The repeat is polled by the
3DS HID loop, which owns the press/release state, so the Android build (whose
tap bridge reports only completed taps) does not repeat yet.

OFF keeps the raw colours and VIVID is a fixed preset, so the six params have
no effect in either mode. Their values and buttons then read faint, and a
step still keeps the value but raises the "USED IN GBA AND CUSTOM" toast, so a
missing effect never reads as a broken button.

The words of the page (COLOURS, MODE, BRIGHTNESS, CONTRAST, SATURATION, GAMMA,
SHADOWS, MIDTONES, GBA, CUSTOM, VIVID, PREVIEW, BACK, CLOSE WITH X OR A TAP
OUTSIDE, USED IN GBA AND CUSTOM) go through the existing text table
(EN/FR/DE/ES/IT).

## Config keys

The grade is persisted in every backend:

- 3DS ini, `platform/3ds/source/port_config_3ds.c`;
- PC/Android `config.json`, `port/port_runtime_config.cpp`;
- `tools/ui-preview/stubs.c`.

| Key | Values |
| --- | --- |
| `colour_mode` | 0 OFF, 1 GBA, 2 CUSTOM, 3 VIVID |
| `colour_brightness` | -50..+50 |
| `colour_contrast` | -50..+50 |
| `colour_saturation` | -50..+50 |
| `colour_gamma` | -20..+20, one twentieth per step (0.0..2.0) |
| `colour_shadows` | -50..+50, 0 neutral |
| `colour_midtones` | -50..+50, 0 neutral |

Out-of-range values are clamped by the parser and again by the module; an
unknown mode reads as OFF.

**Migration.** A file that only has the old `color_correction=1` and no
`colour_mode` loads as GBA; `color_correction=0`, or a missing key, loads as
OFF. When `colour_mode` is present it wins. Builds still write
`color_correction` alongside the new keys, derived as "mode is not OFF", so an
older build reading the same file stays coherent.

The finer gamma step has **no migration**: an old test build that saved
`colour_gamma=-4` meant 0.6, but this build reinterprets it as 0.8
(`1 + (-4) * 0.05`). That only affects configs written by pre-release test
builds; the released builds never had the `colour_gamma` key.

## How it is applied

The math and the installed state live in `port/port_color_grade.c/.h`, a
platform-neutral module. `Port_ColourGrade_Set` clamps the grade, rebuilds the
32-entry per-channel LUT plus the 256-entry shadow/midtone table, and bumps
`Port_ColourGrade_Epoch()` only when something changed. Those rebuilds are the
only place with floating point; per-pixel and per-entry paths are integer
lookups. Saturation is an 8.8 fixed-point luma mix
(`luma = (299r + 587g + 114b) / 1000`), skipped when it is zero.
`Port_ColourGrade_LumaAdjust` hands out the zone table for callers that inline
the pipeline; they rebuild it on the same epoch.

Each site keeps a copy of the LUT and checks the epoch instead of caching a
boolean:

- **Software renderer** (`port/ppu/src/mode1.c`): palette conversion goes
  through a private 32-entry copy of the channel LUT and a 256-entry zone table,
  rebuilt when the epoch moves (together with the saturation factor of the
  installed grade); each word takes the same 8.8 luma mix as the GPU path,
  skipped in OFF, then `channel + adj[luma]` when the zone table is not neutral.
  The BG/OBJ palette LUTs and the 4bpp pair tables are rebuilt on the same
  change.
- **3DS GPU renderer** (`platform/3ds/source/port_ppu_gpu_3ds_model.c`):
  `PpuGpu3DS_PackRgba5551` converts each palette word through the LUT (plus the
  saturation mix and zone lift, in that order) and packs back to 5551.
  `PpuGpu3DS_CacheBeginFrame` compares the epoch against the one it last saw and
  bumps every palette generation, so the tile cache and the view rings decode
  again after a grade change.
- **Second-screen art** (`port/port_second_screen_color.c`):
  `Port_SecondScreen_Rgb555` calls `Port_ColourGrade_ToRgb888`, and
  `Port_SecondScreen_ColorEpoch` returns the module epoch.
- **Scene picture caches** (`platform_gpu_3ds.c`, `scene_pictures_3ds.c`):
  each cached texture records the epoch it was made with; a mismatch remakes it.
- **Desktop present** (`port/port_ppu.cpp`): a 256-entry 8-bit LUT (OFF an
  identity, GBA the existing gamma-4 -> sRGB formula, CUSTOM the module curve)
  is applied to the finished frame, with the saturation mix and the zone lift in
  the same pass. The same pass runs over the GPU supersample buffer.

Every site applies the full transformation, curve plus saturation. The only
remaining difference is the desktop present's domain: it works on 8-bit channels
after the blend, where the others work on the 15-bit palette words.

## Porting checklist (EstebanPdN upstream)

`port/port_color_grade.c/.h` only uses `<stdint.h>` and `<stdbool.h>` and knows
nothing about the 3DS, SDL or this fork, so it can be copied as is.

1. Copy `port/port_color_grade.c` and `port/port_color_grade.h`.
2. Add the seven `colour_*` keys to the upstream `port_config_3ds.c`. Upstream
   already persists `color_correction` in the same ini style, so do the same
   migration: an absent `colour_mode` derives from `color_correction` (true ->
   GBA, false -> OFF), and keep writing `color_correction` as "mode is not OFF".
3. Grep the upstream tree for `color_correction` and `GetColorCorrection` to
   find every place that still branches on the old boolean. Upstream has no
   `port/` layer like this fork's, so those sites are in its own
   `port_ppu_3ds.c` and decode code; replace each one with the module (channel
   LUT for palette conversion, `Port_ColourGrade_Active` for "is a grade on").
4. Keep the invariants: OFF must stay the raw `x << 3` expansion, GBA must stay
   the fixed `kGbaLut`, and any cache that stores converted colours or the zone
   table must be invalidated when `Port_ColourGrade_Epoch()` changes.

Upstream code was not inspected while writing this; the steps above are the
sites to search for, not a claim about their exact contents.

## Test

`port/port_color_grade_test.c` (xmake target `colour_grade_test`) covers OFF and
GBA parity, neutral CUSTOM parity, clamping (gamma ±20, zones ±50), grey
preservation, LUT monotonicity, the epoch (including a zone-only change) and the
mode names, VIVID's parity with CUSTOM `{10, 10, 0, -8}` (LUT bytes and
`ToRgb888`) while its stored fields stay as they were, the shadow/midtone lifts
by luma band, neutral-zone parity, OFF/VIVID ignoring zones, GBA receiving them,
and the all-zero/falling `LumaAdjust` table (linear shadow ramp to 50% luma,
midtone bell peaking at 50% luma, and the GBA black lift at SHADOWS +50).
