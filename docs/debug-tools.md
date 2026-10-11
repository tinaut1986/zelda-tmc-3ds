# On-device debug tools

What the bottom screen offers for debugging, where its files land and how to read
them. Modelled on `../sm-3ds/docs/debug-tools.md` and `../mzm/docs/3ds-debug-tools.md`;
the plan for the rest is PLAN phase 1.

## Builds

Everything below except L+R+A exists only in a debug-tools build (the SD log
and perf recorder code is compiled into every build, but nothing reaches it
without the DEBUG tab):

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

The bug icon in the tab bar (debug builds only).

| Cell | What |
|---|---|
| CONSOLE | NEW 3DS or OLD 3DS (what the performance profile was picked for) |
| FPS NOW, FPS AVG | presented frames per second, now and averaged; green from 59, yellow from 50, red below |
| CORE 1 | the time limit granted on the system core |
| VIEW | WIDE or NATIVE (the widescreen flag the PPU and the culling read) |
| AREA/ROOM | the current area and room, hex |
| LINK X,Y | Link's position in area pixels |
| SWITCHES | opens the performance switches (below) |
| SD LOG | the SD log (below): the square button on the right starts or stops it, the rest of the cell switches BUFFERED / DIRECT |
| PERF RECORDER | the frame-time recorder (below): the side button starts and stops it; while it runs the cell shows the rows held and how full the buffer is |
| LOG MARK | writes `--- MARK n ---` into the log and flushes it: tap it when the problem shows, then find it in the file |
| MEM DUMP | writes a quick dump, as L+R+A does (below) |
| LOAD STATE | loads the game state of the newest dump, after a confirmation |
| SHOW FPS | the top-screen FPS box |

Cells for something that runs work as in mzm and sm-3ds: the button on their
right starts it (green triangle) or stops it (red square). A toast at the bottom
says what happened (file number, rows written).

### SWITCHES

The performance knobs that upstream left in `tmc3ds.ini` only, on the console.
Each tap cycles the value and saves it (white: the default, blue: changed). LIVE
ones are read every frame and take effect at once; AT NEXT BOOT ones are read
when the game starts and show yellow until it is restarted.

| Switch | Key | Values | What |
|---|---|---|---|
| RENDERER | `gpu_renderer` | GPU / CPU | the PICA200 tile renderer or the software PPU. The GPU renderer only exists on Old 3DS; a New 3DS shows `CPU (N3DS)` |
| FRAME SYNC | `gpu_frame_sync` | ON / OFF | wait for the previous GPU frame before rewriting its buffers (off by default: it coincided with a black screen) |
| VBLANK LOCK | `vblank_phase_lock` | ON / OFF | lock the presentation to the next vblank instead of a pending one |
| STATIC QUAD | `gpu_static_quad` | ON / OFF | reuse the presenter's quad |
| MAP SKIP | `bottom_map_skip` | ON / OFF | skip MAP-tab repaints that would draw the same picture |
| FRAME LOG | `frame_log` | ON / OFF | upstream's per-line `tmc3ds.log` (opens and closes the file per line: slow, it causes stalls) |
| BOTTOM CORE | `bottom_core` | AUTO / CORE 1 | where the bottom-screen painter runs |
| AUDIO CORE | `audio_core` | AUTO / CORE 0 / CORE 1 | where the audio worker runs |
| COMPACT UP | `compact_upload` | ON / OFF | smaller upload surfaces on Old 3DS |
| BOTTOM 565 | `bottom_rgb565` | ON / OFF | bottom screen in RGB565 |
| CPU LIMIT | `app_cpu_limit` | AUTO / 80 / 70 / 50 / 30 % | the app's share of core 1 (the rest goes to GSP and the sysmodules) |

Upstream's comments on each are in `platform/3ds/source/port_config_3ds.c`. Every
change is also written to the SD log when it runs.

## SD log

`debug/tmc-log-NNNN.txt` in the data folder. Debug builds start it at boot (one
file per session; the SD LOG cell stops and restarts it, each start is a new
file). It holds the version, the console, core 1, kernel and free memory, every
switch change, the marks, and every 5 s a perf line: frames shown and logic ticks
per second, presentations skipped, engine work per logic tick (and its max),
vblank wait per frame, GPU frames and software fallbacks, tiles decoded, area and
room, free memory. BUFFERED (default): lines collect in a 16 KB buffer, written when
it fills, on a mark, on a mode change, on stop and on exit. DIRECT: each line is
written and flushed at once, so a hang cannot swallow the last lines.

## Perf recorder

One row per presented frame into RAM while it runs (up to 36000 rows, ten minutes
at 60 fps, ~2 MB); nothing is written until it is stopped, when the rows become
`debug/tmc-perf-NNNN.csv` (the game freezes a moment while it writes). Columns:

| Column | What |
|---|---|
| `frame`, `t_ms` | row number and time since the first row |
| `interval_ms` | since the previous row's start: 16.7 is 60 fps, 33.4 a missed vblank |
| `work_ms` | the engine loop iteration (game logic and rendering, without the vblank wait) |
| `vblank_ms` | the last wait for vblank |
| `gpu_begin_ms` | GPU path: waiting for the previous GPU frame (`C3D_FrameBegin`) |
| `gpu_build_ms` | GPU path: building the frame's commands and uploading tiles (preflight) |
| `sw_render_ms` | the software PPU, when the GPU did not draw the frame |
| `top_ms` | the whole top-screen part of the present |
| `bottom_ms` | the bottom-screen paint that finished in this frame (0 when none did) |
| `logic_frames` | logic ticks since the previous row: more than 1 means presentations were skipped |
| `gpu` | 1 when the GPU renderer drew the frame |
| `area`, `room` | where the game was (FF outside gameplay) |

On the PC: `tools/perf-csv/analyze.py FILE.csv [--worst N] [--by-room]` prints the
frame rate, interval percentiles, missed vblanks, the mean / p95 / max of each part
and the worst frames with their parts.

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
tabs draw exactly as on the console (with stand-in values for the log, recorder
and switches). `THEME=n` picks the theme. The shots and the taps that reach them are
the `SHOTS` list in the script.
