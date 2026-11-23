# Colour Grade — Design

Date: 2026-11-23. Branch: `feat/colour-grade` (cut from `release/v0.4.7`).

## Intent and why

The COLOURS option is a binary toggle applying a fixed GBA-LCD curve
(`kLcdLut`, 32 entries per channel) that crushes blacks and oversaturates.
Players want control over the look, not just on/off. This replaces the toggle
with a three-mode colour grade — OFF / GBA / CUSTOM — whose CUSTOM mode
exposes brightness, contrast, saturation and gamma, edited live from a new
bottom-screen COLOURS page.

## Goals

- OFF: byte-exact with today's raw expansion (channel = 5-bit << 3).
- GBA: byte-exact with today's fixed LUT.
- CUSTOM: parameterised curve, integer math on hot paths (LUT rebuilt only on
  settings change), saturation via fixed-point luma mix.
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

Bottom screen OPTIONS -> COLOURS cell now shows the mode (OFF/GBA/CUSTOM) and
opens a new `SS_SETTINGS_COLOURS` page (same mechanism as the UPDATE page).
Rows: MODE (3 stops), BRIGHTNESS/CONTRAST/SATURATION (-50..+50, step 10),
GAMMA (0.4..1.6, step 0.1 shown as "1.3"). Tap cycles, like VOLUME today.
A live preview strip of game palette colours is painted on the page.
New words go through the i18n table (EN/FR/DE/ES/IT).
