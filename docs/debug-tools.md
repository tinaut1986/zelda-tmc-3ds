# On-device debug tools

What the bottom screen offers for debugging, where its files land and how to read
them. Modelled on `../sm-3ds/docs/debug-tools.md` and `../mzm/docs/3ds-debug-tools.md`;
the plan for the rest is PLAN phase 1.

## Builds

Everything below except L+R+A exists only in a debug-tools build:

```sh
./build_3ds.sh                                 # asks; debug is the default
TMC3DS_DEBUG_TOOLS=1 ./platform/3ds/build.sh
```

The switch reaches the code through the generated `tmc3ds_build_config.h`
(`TMC3DS_DEBUG_TOOLS`), which CMake rewrites only when the value changes, so
switching it needs no clean build. A plain build is a production build. The release
workflow passes 1 for Beta builds (tag not reachable from `main`) and 0 for Release
ones, so stable CIAs have none of this.

## DEBUG tab

The fifth tab of the bottom screen (debug builds only).

| Cell | What |
|---|---|
| CONSOLE | NEW 3DS or OLD 3DS (what the performance profile was picked for) |
| FPS NOW, FPS AVG | presented frames per second, now and averaged; green from 59, yellow from 50, red below |
| CORE 1 | the time limit granted on the system core |
| VIEW | WIDE or NATIVE (the widescreen flag the PPU and the culling read) |
| AREA/ROOM | the current area and room, hex |
| LINK X,Y | Link's position in area pixels |
| MEM DUMP | writes a quick dump, as L+R+A does (below) |
| LOAD STATE | loads the game state of the newest dump, after a confirmation |
| SHOW FPS | the top-screen FPS box |

## L+R+A: quick dump (all builds)

Pauses the game, shows `DUMP SAVED` and writes `dumps/dump-*` in the data folder
(`sdmc:/3ds/Minish Cap 3DS/`): top and bottom framebuffers (BMP and raw), EWRAM,
IWRAM, VRAM, palettes, OAM, I/O, game state, frame cadence, adaptive-skip data,
per-core PPU timings, GPU work, audio buffer health, save state, memory and input.
Upstream's feature; fetch the folder over FTP.

## On the PC

`tools/ui-preview/build.sh [OUTDIR]` renders the bottom screen's tabs to PNG
without a console or a ROM (`TMC3DS_DEBUG_TOOLS=0` for a production build's view).
Without a ROM the map and item art take their fallbacks; the OPTIONS and DEBUG
tabs draw exactly as on the console. The shots and the taps that reach them are
the `SHOTS` list in the script.
