# Colour Grade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the binary COLOURS toggle with a three-mode colour grade (OFF / GBA / CUSTOM) with brightness, contrast, saturation and gamma, edited from a new bottom-screen COLOURS page.

**Architecture:** A platform-neutral transform module (`port/port_color_grade.c`) owns the math and a global grade + invalidation epoch; every site that today branches on `Port_Config_GetColorCorrection()` reads the module instead. Config gains five persisted keys in all three backends. The bottom-screen UI gains an `SS_SETTINGS_COLOURS` page.

**Tech Stack:** C99 (port), C++ (desktop config), xmake host targets, devkitARM CMake for the 3DS build.

**Spec:** `docs/superpowers/specs/2026-11-23-colour-grade-design.md`

## Global Constraints

- Code, comments and commit messages in English (CLAUDE.md).
- GBA mode byte-exact with the fixed LUT: {0,0,0,0,1,2,4,8,13,19,25,32,39,46,54,63,71,80,90,100,110,120,131,142,154,165,178,190,203,216,229,243}.
- OFF mode byte-exact with today's raw expansion: channel = 5-bit << 3.
- No floats on per-frame/per-pixel 3DS paths; float math allowed only when (re)building the 32-entry LUT on a settings change.
- No new dependencies; the module uses only <stdint.h>/<stdbool.h>.
- The PC port keeps compiling; `#ifdef TMC_3DS` for 3DS-only code.
- Do not churn inherited style; touch only what this feature needs.

## Review Focus

- Out-of-range config values (mode > 2, brightness 200, gamma 99): clamped at parse AND at `Port_ColourGrade_Set`; unknown mode parses to OFF. Task 1 test.
- Legacy migration `color_correction=1` without `colour_mode` -> GBA in both parsers. Task 2.
- Stale caches: every site caching a `corrected` bool must switch to the epoch, or a CUSTOM tweak leaves old art. Task 3.
- Saturation must not disturb greys (r==g==b in, r==g==b out). Task 1 test.
- CUSTOM neutral (all params 0) byte-equal to OFF. Task 1 test.

## Interfaces (module API, locked)

    typedef enum { PORT_COLOUR_OFF = 0, PORT_COLOUR_GBA, PORT_COLOUR_CUSTOM, PORT_COLOUR_MODE_COUNT } PortColourMode;
    typedef struct { uint8_t mode; int8_t brightness, contrast, saturation, gamma; } PortColourGrade;
    void Port_ColourGrade_Set(const PortColourGrade* g);   /* clamps, installs, bumps epoch only on change */
    const PortColourGrade* Port_ColourGrade_Current(void);
    unsigned Port_ColourGrade_Epoch(void);
    bool Port_ColourGrade_Active(const PortColourGrade* g);      /* mode != OFF */
    void Port_ColourGrade_ChannelLut(const PortColourGrade* g, uint8_t lut[32]);
    void Port_ColourGrade_ToRgb888(const PortColourGrade* g, uint16_t rgb555, uint8_t out[3]);
    const char* Port_ColourModeName(unsigned mode);

CUSTOM curve (per channel, x = 0..31): t = (x<<3)/255.0;
t = pow(t, 1.0/(1.0 + gamma*0.1)) with gamma in -6..+6;
t = (t - 0.5)*(1.0 + contrast/50.0) + 0.5 with contrast in -50..+50;
t += brightness/100.0 with brightness in -50..+50; clamp; round to 0..255.
Saturation (only GBA/CUSTOM): luma = (299r+587g+114b)/1000;
out = luma + (c - luma) * ((50+saturation)/50), fixed-point 8.8, clamp.

## Tasks

### Task 1: Core module + host test

Files: create `port/port_color_grade.h`, `port/port_color_grade.c`,
`port/port_color_grade_test.c`; modify `xmake.lua` (add the .c to every
target that compiles `port/port_second_screen_color.c`; new host target
`colour_grade_test` following the `rng_golden_test` pattern; also add the .c
to the 3DS build file list wherever `port_second_screen_color.c` is listed).
TDD: test first (OFF parity, GBA parity, neutral CUSTOM parity, clamping,
grey preservation, epoch behaviour), then implement. Commit
`feat: colour grade core module`.

### Task 2: Config backends

Files: modify `port/port_runtime_config.h` (declare
`Port_Config_GetColourGrade(PortColourGrade*)` /
`Port_Config_SetColourGrade(const PortColourGrade*)`),
`port/port_runtime_config.cpp` (config.json keys `colour_mode`,
`colour_brightness`, `colour_contrast`, `colour_saturation`, `colour_gamma`,
migration from `color_correction`), `platform/3ds/source/port_config_3ds.c`
(same keys in the ini parser/serializer, same migration; keep writing
`color_correction` too so older builds stay in sync),
`tools/ui-preview/stubs.c` (backing static).
`Port_Config_SetColourGrade` persists all five keys, then calls
`Port_ColourGrade_Set` (live). Startup sites push the grade into the module
(3DS: `port_ppu_3ds.c` init where `virtuappu_mode1_set_color_correction` is
called today; PC: `port_ppu.cpp` where `Port_Config_GetColorCorrection()` is
read at startup). `Port_Config_SetColorCorrection` routes to the mode
(on -> GBA, off -> OFF) and writes both keys. Commit
`feat: colour grade config keys in all backends`.

### Task 3: Renderer wiring

Files: `port/ppu/src/mode1.c` + `port/ppu/include/cpu/mode1.h` (drop the bool
and the private LUT; `virtuappu_mode1_rgb555_to_abgr8888` reads the module,
rebuilding a private lut copy only when the epoch moves; drop
`virtuappu_mode1_set_color_correction` and its call sites in
`port/ppu/tests/mode1_native_fast_path_test.c`,
`port/ppu/tests/mode1_draw_order_profile_test.c`,
`platform/3ds/tests/port_ppu_gpu_3ds_bench.c`), `platform/3ds/source/port_ppu_3ds.c`
(replace the startup toggle with the grade push; remove the now-unused
`Port_PPU_SetColorCorrection`/`Port_PPU_ColorCorrectionEnabled`),
`platform/3ds/source/port_ppu_gpu_3ds_model.c` + `.h`
(`PpuGpu3DS_PackRgba5551` via `Port_ColourGrade_ToRgb888` then >>3 per
channel; the generation bump in `PpuGpu3DS_CacheBeginFrame` compares a cached
epoch against `Port_ColourGrade_Epoch()`; drop `PpuGpu3DS_SetColorCorrection`),
`platform/3ds/source/port_ppu_gpu_3ds.c` (the three
`Port_Config_GetColorCorrection()` reads become
`Port_ColourGrade_Active(Port_ColourGrade_Current())`),
`platform/3ds/source/platform_gpu_3ds.c` and `scene_pictures_3ds.c`
(`picture->corrected` bool -> cached grade epoch), `port/port_second_screen_color.c`
(`Port_SecondScreen_Rgb555` via the module; `Port_SecondScreen_ColorEpoch`
returns `Port_ColourGrade_Epoch()`), `port/port_ppu.cpp` (build the present
LUT from the grade: OFF -> no pass, GBA -> the existing gamma-4->sRGB
formula, CUSTOM -> the module curve; saturation folded into the per-pixel
pass), `port/port_second_screen.c` (the `SS_SET_COLOR_CORRECTION` handler:
under `TMC_3DS` it cycles OFF->GBA->CUSTOM through
`Port_Config_SetColourGrade`; other platforms keep the legacy bool pair).
Commit `feat: apply the colour grade in every renderer`.

### Task 4: Bottom-screen UI + i18n

Files: `port/port_second_screen.c` (new `SS_SETTINGS_COLOURS` page enum;
`SettingsPageTitle` "COLOURS"; `SettingsBackPage` -> ROOT;
`SettingsPageRows` under `TMC_3DS` -> `SS_SET_COLOUR_MODE`,
`SS_SET_COLOUR_BRIGHTNESS`, `SS_SET_COLOUR_CONTRAST`,
`SS_SET_COLOUR_SATURATION`, `SS_SET_COLOUR_GAMMA` (new `SS_SET_*` values in
the `TMC_3DS` block of the enum); `GetSettingState` cases: mode name via
`Port_Text_Tr`, params "+10"/"0"/"-20", gamma "1.3" one decimal; tap handler
cycles and calls `Port_Config_SetColourGrade`; a live preview strip of 8
fixed RGB555 game colours through `Port_ColourGrade_ToRgb888` painted under
the rows of the COLOURS page), `platform/3ds/source/bottom_compact_3ds.inc`
(the COLOURS cell at the OPTIONS list shows the mode name and opens the page
via `SS_ACT_SETTINGS_PAGE`/`SS_SETTINGS_COLOURS` instead of toggling),
`port/port_text_i18n.c` (new words MODE, BRIGHTNESS, CONTRAST, SATURATION,
GAMMA, GBA, CUSTOM with EN/FR/DE/ES/IT), `port/port_text_i18n_test.c`
(extended word list). Commit `feat: COLOURS page on the bottom screen`.

### Task 5: Docs + full host verification

Files: create `docs/colour-grade.md` (how the grade works, the config keys,
and an upstream porting checklist). Run every host test target touched or
existing (colour_grade_test, i18n test, mode1 tests) and the host build.
Commit `docs: colour grade feature doc`.

### Task 6: Branch push, PR, CIA

Push `feat/colour-grade`; open the PR into `release/v0.4.7` of
`tinaut1986/zelda-tmc-3ds` (fork under the contributor account if there is
no write access). The `Build 3DS` workflow runs on the PR and uploads
`tmc-3ds-packages` (cia + 3dsx); download the CIA artifact and hand it to
the user.
