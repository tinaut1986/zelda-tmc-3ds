# Colour Grade — Design

Date: 2026-11-23. Branch: `feat/colour-grade` (cut from `release/v0.4.7`).

## Intent and why

The COLOURS option is a binary toggle applying a fixed GBA-LCD curve
(`kLcdLut`, 32 entries per channel) that crushes blacks and oversaturates.
Players want control over the look, not just on/off. This replaces the toggle
with a four-mode colour grade — OFF / GBA / CUSTOM / VIVID — whose CUSTOM mode
exposes brightness, contrast, saturation and gamma, edited live from a new
bottom-screen COLOURS page. VIVID is a fixed CUSTOM preset (brightness +10,
contrast +10, saturation 0, gamma 0.6) and keeps the player's CUSTOM fields
untouched while selected.

## Goals

- OFF: byte-exact with today's raw expansion (channel = 5-bit << 3).
- GBA: byte-exact with today's fixed LUT.
- CUSTOM: parameterised curve, integer math on hot paths (LUT rebuilt only on
  settings change), saturation via fixed-point luma mix.
- VIVID: a fourth persisted mode (value 3, appended so 0/1/2 never shift) that
  builds the CUSTOM curve from a fixed preset — brightness +10, contrast +10,
  saturation 0, gamma 0.6 (stored as -4) — and ignores, but preserves, the
  player's CUSTOM fields.
- Live apply everywhere COLOURS is applied today: software renderer
  (`port/ppu/src/mode1.c`), 3DS GPU renderer (`platform/3ds/source/port_ppu_gpu_3ds_model.c`),
  second-screen art (`port/port_second_screen_color.c`), scene picture caches
  (`platform_gpu_3ds.c`, `scene_pictures_3ds.c`), desktop present post-process
  (`port/port_ppu.cpp`).
- Persisted in every config backend: 3DS ini (`port_config_3ds.c`),
  PC/Android config.json (`port_runtime_config.cpp`), ui-preview stubs.
- Migration: `color_correction=1` with no `colour_mode` key -> GBA (existing
  users see no change).

## Non-goals

- No per-pixel shader post-process on the 3DS (Pica200 budget; decode-time
  transform is the approach).
- No imgui sliders on PC (F8 checkbox keeps working; maps OFF <-> GBA).
- No hue control in v1.

## Approaches considered

1. Parameterise the existing decode-time curve (chosen): reuses all
   cache/epoch plumbing; runs in both 3DS renderers; cheap.
2. Per-pixel post-process like ReShade: right for PC, impractical on 3DS.
3. Named presets only: simpler but not "customisable".

## Portability

`port/port_color_grade.c/.h` is platform-neutral (only <stdint.h>,
<stdbool.h>) so the same module drops into EstebanPdN's upstream later:
upstream already persists `color_correction` in its own `port_config_3ds.c`
and has the same decode sites in kind. A porting checklist lives in
`docs/colour-grade.md`.

## UI

Bottom screen OPTIONS -> COLOURS cell shows the mode (OFF/GBA/CUSTOM/VIVID)
and opens `SS_SETTINGS_COLOURS`, painted in the compact OPTIONS style (the
`bottom_compact_3ds.inc` grid, not the parchment panel): a MODE cell, a BACK
cell and four field rows — BRIGHTNESS/CONTRAST/SATURATION (-50..+50) and
GAMMA (0.4..1.6, shown as "1.3") — each as name, value and a `-` / `+`
button. A step is 1 (gamma 0.1) and clamps, never wraps; MODE cycles its 4
stops. A PREVIEW strip of eight game palette colours sits at the foot.
New words go through the i18n table (EN/FR/DE/ES/IT), VIVID included.

## Amendment: finer gamma, shadow and midtone zones

GAMMA becomes -20..+20 in twentieths (`factor = 1 + gamma * 0.05`, displayed as
`100 + gamma * 5` hundredths); VIVID's 0.6 is now stored as -8. The config
parser and `Port_ColourGrade_Set` clamp to ±20 with no migration for configs
written by pre-release test builds (they reinterpret the saved step).

Two additive luma zones join the grade: SHADOWS -50..+50, weighted
`((96-l)/96)²` below luma 96, and MIDTONES -50..+50, weighted
`4*(l/255)*(1-l/255)`. Their per-channel lift `(zone/100)*255*w` is applied to
every channel after the curve and the saturation mix, in GBA and CUSTOM only
(OFF and VIVID ignore them). The module rebuilds a 256-entry `int16_t` table with
the LUT and exposes it as `Port_ColourGrade_LumaAdjust`; `ChannelLut` stays
curve-only. Every renderer caches the table on the grade epoch, and the epoch
includes the two new fields.

## Amendment: COLOURS page zones and MODE popup

The COLOURS page now scrolls (drag, like OPTIONS) and lists SHADOWS and
MIDTONES as two more `[name][value][-][+]` rows, so the seven rows plus the
PREVIEW strip no longer fit a 240-line screen at once. Tapping MODE opens a
floating popup over the dimmed page instead of cycling in place: OFF, GBA, VIVID
and CUSTOM rows, each with four mini-swatches of that preset, the current row
outlined. A row applies the mode and closes the popup; a tap outside closes it
with no change. The page's `SS_SET_*` ids grow SHADOWS and MIDTONES, and the new
`SS_ACT_COLOUR_MODE_OPEN/SET/CLOSE` actions carry the popup.
