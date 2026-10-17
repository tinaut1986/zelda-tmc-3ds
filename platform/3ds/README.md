# The Minish Cap 3DS Platform

This target builds the native dual-screen Nintendo 3DS frontend.

Outside gameplay, the bottom screen shows a procedural gold-framed Triforce;
touching it opens the existing Minish Cap Settings hierarchy.

## Console Installation

Install the CIA from the latest release (or `build-3ds/game/tmc-3ds-v<VERSION>.cia`):

```text
tmc-3ds-v<VERSION>.cia
```

Then create this directory on the SD card:

```text
sdmc:/3ds/Minish Cap 3DS/
```

Place a legally obtained clean ROM there. Any `.gba` filename is accepted.
Without a ROM there, the one in the original port's folder
(`sdmc:/3ds/The Minish Cap 3DS/`) is read in place, so it is not on the SD
twice. At the first launch with no save of its own, if the original port's
folder has saves (`*.sav`), the bottom screen asks whether to copy them here
(A) or not (B); the answer is kept in `upstream-import.txt` and not asked
again. The original folder is never written to.
The port detects USA and European game codes at runtime and activates the
matching internal data profile before boot. Expected SHA-1 values:

```text
USA:    b4bd50e4131b027c334547b4524e2dbbd4227130
Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7
```

The ROM is read locally from the SD card and is never copied into the CIA.

Audio requires a working 3DS DSP firmware setup. On Luma3DS, use Rosalina's
`Dump DSP firmware` option if homebrew audio is unavailable.

## Controls

- A, B, R, L, START, SELECT and the D-pad / Circle Pad as on the GBA.
- **X and Y** (also ZL and ZR) use an item of their own, like B. Assign it on the bottom
  screen: tap A, B, X or Y in the right-hand column (the button stays selected), then the
  item in ITEMS; tapping X's or Y's item again clears that button. X/Y assignments are kept
  for every save file (`tmc.softslots` in the data folder); in a save without that item the
  button does nothing.

## Display

- Top screen: selectable Wide, Original, and Stretch aspect ratios.
- Display styles, in menu order: linearly filtered Blur, 2x sharp Bilinear,
  and centered one-pixel-per-source-pixel Pixel Perfect. Fresh installations
  start with Bilinear and Wide; valid saved choices are restored on later
  launches.
- Full View: on New 3DS and Old 3DS/2DS, selecting Wide + Pixel Perfect renders
  every room at 400x240; a room smaller than that is centred with black borders and
  interiors keep the 1x pixel size (no zoom). Dialogues keep the 400x240 view (the text box is centred); room-name
  banners, transitions and unsupported
  effects fall back safely to the established presentation.
- Scenes the GBA drew as one fixed 240x160 picture are extended in Full View with generated
  pictures in the romfs (`docs/wide-scene-extension.md`): the Minish portal cutscene (falling into
  the mushroom ring) at 400x240, its 256x320 picture centred, 240 lines of it at once; the title
  screen at 400x240, its forest extended to the whole view, the light rays coming from the top
  and the sword moving away across it. A picture of one's own in the data folder
  (`portal-cutscene-strips.bin`, `title-forest.bin`, from `tools/scene-extend/to_console.py`)
  replaces the shipped one.
- Bottom screen: 320x240 map, dungeon/status information and touch item UI.
- Rendering: the GPU renderer (PICA200, Citro3D) draws every layer; the software GBA PPU
  stays as the reference for GPU CHECK and as the fallback.
- Performance profile: selected automatically from the detected console model.
- New 3DS: requests 804 MHz, L2 cache and access to the extra application core.
- New 3DS turbo: hold the C-stick in any direction and select 2x through 5x
  game speed from the Gameplay settings.
- Old 3DS: experimental CPU-renderer and audio optimizations plus adaptive
  presentation skipping target 60 Hz engine timing when visual rendering falls
  behind. Input, touch, audio and lifecycle processing continue on skipped
  presentation ticks; visual FPS may be lower.
- Bottom-screen scheduling: touch state is sampled on every engine tick;
  interactive hitboxes are promoted with the physically visible buffer, and
  unchanged static pages avoid redundant paint/upload work.
- Bottom screen: a bar of icon tabs (map, items, quest, save states, achievements, options and,
  in debug builds, the bug icon for DEBUG). OPTIONS holds the display, turbo, theme, tab
  position and UPDATES channel.
- WIDE + AI (`wide_ai`, `wide_ai_asked` in `tmc3ds.ini`), after `../mzm`: the scene extensions made with a
  generative AI (the portal's sides, the title's forest, the file select's cave) are shown only in WIDE + AI,
  the second stop of OPTIONS -> VIEW (WIDE, WIDE + AI, ORIGINAL). The first time WIDE is on, the bottom screen
  asks once whether to show them (YES is WIDE + AI); the answer is kept.
- Show FPS: a compact lower-left top-screen box of four lines, averaged over
  the last second: the game's rate (green from 59, yellow from 50, red below),
  `L` the logic's ms per tick, `G` the GPU's ms per presented frame and `S` the
  rate frames reach the screen.
- Diagnostics (debug builds): the DEBUG tab's SCREEN DUMP writes numbered `debug/tmc-dump-*`
  files (top and bottom screenshots, memories, state, performance data); there is no button
  chord. See `docs/debug-tools.md`.
- System lifecycle: HOME, sleep and application close events are handled by the
  regular 3DS applet loop.
- Boot: the top screen shows the logo (`romfs/splash.rgb565`, 400x240, the
  whole logo inside a margin) and the bottom a gold triforce turning while the
  ROM loads (`Platform3DS_BootAnimStart`/`Stop`); no boot text and no fixed
  delay. A fatal message (no ROM, storage) still uses the text console. Once
  gameplay begins, stdout/stderr logs are detached from the bottom
  framebuffer so they cannot flicker over the map or touch interface.

The CIA metadata uses a stable title ID and requests SD card access for local
ROM and save data.

## Requirements

- devkitPro with devkitARM, libctru, Citro2D and Citro3D
- CMake with the devkitPro Nintendo 3DS toolchain
- `makerom` and `bannertool` for CIA packaging

Run `./build_3ds.sh` from the repository root (see the README's Building section), or the
bare build:

```sh
chmod +x platform/3ds/build.sh
TMC3DS_DEBUG_TOOLS=1 platform/3ds/build.sh   # 0 for a production build
```

The CIA (and a `.3dsx` for local use) is written under `build-3ds/game/`. No ROM, extracted
asset package or save data is included.
