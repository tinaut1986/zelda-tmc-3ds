# On-device debug tools

What the bottom screen offers for debugging, where its files land and how to read
them. Modelled on `../sm-3ds/docs/debug-tools.md` and `../mzm/docs/3ds-debug-tools.md`;
the plan for the rest is PLAN phase 1.

## Builds

Everything below exists only in a debug-tools build (the SD log and perf recorder
code is compiled into every build, but nothing reaches it without the DEBUG tab;
there is no button chord any more: upstream's L+R+A is gone). The one exception is the
install-only FTP server of a production build (`dev-ftp.txt`, "FTP server" below):

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
| SCREEN DUMP | both screens, the memories and the frame's state as numbered `debug/tmc-dump-NNNN-*` files, written at the next frame boundary (below) |
| SHOW FPS | the top-screen numbers box: game rate, `L` logic ms, `G` GPU ms, `S` presented rate |
| FTP SERVER | an FTP server inside the game (below): the side button starts or stops it, the cell shows the address to connect to |
| GPU CHECK | the next frame is drawn by both renderers and compared (as in sm-3ds); the cell shows the checks done and the pixels that differed. A frame that differs is saved as `debug/tmc-check-NNNN-*` (CPU and GPU surfaces, the first differing pixels). A difference the blender cannot explain is counted and logged, it no longer retires the renderer. Old 3DS only (the only model with the GPU renderer) |

Cells for something that runs work as in mzm and sm-3ds: the button on their
right starts it (green triangle) or stops it (red square). A toast at the bottom
says what happened (file number, rows written).

### SWITCHES

The performance knobs that upstream left in `tmc3ds.ini` only, on the console.
Each tap cycles the value, applies it at once and saves it (white: the default,
blue: changed). None needs a restart.

| Switch | Key | Values | What |
|---|---|---|---|
| RENDERER | `gpu_renderer` | GPU / CPU (TEST) | the GPU renderer (both models) or the software PPU, only to compare against |
| FRAME SYNC | `gpu_frame_sync` | ON / OFF | wait for the previous GPU frame before rewriting its buffers (off by default: it coincided with a black screen) |
| VBLANK LOCK | `vblank_phase_lock` | ON / OFF | lock the presentation to the next vblank instead of a pending one |
| STATIC QUAD | `gpu_static_quad` | ON / OFF | reuse the presenter's quad |
| MAP SKIP | `bottom_map_skip` | ON / OFF | skip MAP-tab repaints that would draw the same picture |
| BOTTOM CORE | `bottom_core` | AUTO / CORE 0 / CORE 1 | where the bottom-screen painter runs (AUTO: core 1 on the Old 3DS, core 0 on the New 3DS); its thread is recreated before the next paint |
| AUDIO CORE | `audio_core` | AUTO / CORE 0 / CORE 1 | where the audio refill thread runs; it is recreated at once (the queued buffers cover the gap) |
| COMPACT UP | `compact_upload` | ON / OFF | smaller upload surfaces, Old 3DS only; taken at the start of the next frame |
| CPU LIMIT | `app_cpu_limit` | AUTO / 80 / 70 / 50 / 30 % | the app's share of core 1 (the rest goes to GSP and the sysmodules); the cell shows the share granted |
| STEREO 3D | `stereo_3d_on` | OFF / ON | stereoscopic 3D on the top screen while the slider is up; ON by default (`docs/stereo-3d.md`) |

`bottom_rgb565` is not offered: upstream's own note says it turns the bottom
screen red. Upstream's comments on each are in `platform/3ds/source/port_config_3ds.c`.
Every change is also written to the SD log when it runs.

## FTP server

DEBUG -> FTP SERVER starts a small FTP server inside the game, so files can be
fetched or dropped while it keeps running: no closing the game, no FBI or ftpd.
The cell shows `IP:5000` (the same port as FBI and ftpd, so `./build_3ds.sh --ftp`
and the usual client bookmarks work), a toast says it again, and the cell's title
changes while a client is connected. It needs Wi-Fi; the cell says why when it
cannot start (`NO WI-FI`, `PORT 5000 BUSY`).

The DEBUG tab's tools come back at the next launch as they were left (`tmc3ds.ini`:
`debug_log`, `debug_log_direct`, `debug_ftp`; debug builds only, a production build ignores
and drops them). An FTP server left on starts again by itself, retrying every ~2 s until
Wi-Fi is up; tapping its button while it waits cancels it. The perf recorder and GPU CHECK
are one-shot captures and always start off.

Stopping it waits up to 3 s for the server thread (a link request waiting for the game gives up
at once). A thread that does not end is left running
(the cell says `STUCK, RESTART THE GAME` and no new server starts until the game is restarted):
freeing it, or the network service under it, froze the game. `tmc3ds.log` (written straight to
the SD card, so it survives switching the console off) records each stop (`ftp stop: server
thread <what it was doing>`, `ftp stop: done`), a thread busy in one command for 30 s
(`WATCHDOG: ftp server thread busy: command PEEK for 30.2 s`), and, when the main thread stops,
what the server thread was doing (`WATCHDOG: main thread stopped at stage N ... (ftp server:
...)`; stages 30-34 are the steps of the stop itself).

- Anonymous: any user name and password.
- `/` is the root of the SD card; the game's files are in
  `/3ds/Minish Cap 3DS/` (`debug/`, saves, `tmc3ds.ini`).
- Passive mode only (PASV and EPSV, data ports 5001-5064), one client at a time.
  FileZilla, WinSCP, lftp and curl work:

```sh
curl ftp://<3ds-ip>:5000/3ds/Minish%20Cap%203DS/debug/                       # list
curl -O ftp://<3ds-ip>:5000/3ds/Minish%20Cap%203DS/debug/tmc-perf-0001.csv   # fetch
wget -r -nH --cut-dirs=3 ftp://<3ds-ip>:5000/3ds/Minish%20Cap%203DS/debug/   # the whole folder
```

`SITE INSTALL <path>` installs a CIA of this game that is already on the SD card
over the running game and relaunches into the new build 10 s later, through libctru's chainloader
(jumping at once made the system loader crash out of memory, issue #4); the FTP server comes back
on with it.
`./build_3ds.sh --install IP` builds, uploads to
`/3ds/Minish Cap 3DS/update/dev.cia` and installs in one go; the interactive menu
offers it as "Install into the running game". Any unsaved progress is lost. While a
`.cia` is being received, while it installs and during the 10 s wait before the restart, the bottom
screen says so on any tab, with a percent and a bar: `RECEIVING NEW BUILD 42%`, `INSTALLING NEW BUILD
80%`, `RESTARTING IN 7 S`. The upload's total comes from `ALLO <bytes>`, which the client sends before
`STOR` (`--install` does: `curl -Q "+ALLO <size>"`, the `+` meaning "before the transfer"); without it
the notice shows no percent and no bar (`RECEIVING NEW BUILD...`).
Keep `debug/` small: a listing of it with over a thousand files (old dumps and GPU checks) hung the
server; fetch known names (`tmc-dump-NNNN-vram.bin`, `tmc-check-last.txt`) instead of listing.

**Install-only server in a production build.** A production build has no DEBUG tab, so a new
build could only be installed with FBI. If `dev-ftp.txt` (any content, empty is fine) is in
`/3ds/Minish Cap 3DS/`, a production build starts a reduced server once Wi-Fi is up (it retries
every ~2 s): still anonymous, but only `/3ds/Minish Cap 3DS/update/` can be listed, read or
written (any other path answers `550 Not allowed.`; its parent folders can be entered to walk
down to it), and the remote control (every `SITE` but INSTALL) answers `504`. So
`./build_3ds.sh --install IP` works on it as on a debug build, with either build mode. The bottom
screen shows a bar over every tab, `FTP - INSTALL ONLY` with the address and CANCEL; taps under
the bar reach nothing else. CANCEL stops the server for the rest of the session (leave the game
and open it again to get it back). A debug build ignores the file (it has the DEBUG tab's server).
Create the file once, while a debug build has its FTP on:
`curl -T /dev/null ftp://<ip>:5000/3ds/Minish%20Cap%203DS/dev-ftp.txt`. Delete it to turn it
off. Anyone on the same network can send a CIA while the server runs, and the title-ID check
only stops a CIA of *another* app (`cia/tmc3ds.rsf` is public): keep the file off on networks
you do not trust. Players never see it and it is not in the release notes.

Its thread runs on core 1 below the game's priority and sleeps on the network,
so it costs nothing while idle; a transfer competes with the game for the SD
card. It shares the network service with the updater (`net_3ds.c`), so both
can run together. Transfers are written to the SD log.

## Remote control

While the FTP server runs, the PC can drive the game through SITE commands
(`platform/3ds/source/debug_link_3ds.h`): hold buttons for N frames, tap the bottom
screen, take screenshots, save and load save states (docs/save-states.md), take a dump,
cycle a switch, read the game state and read or write memory. `tools/tmc-link/tmc-link.py` is the
client (standard-library Python):

```sh
tools/tmc-link/tmc-link.py wait                 # until the game answers (after an install)
tools/tmc-link/tmc-link.py state                # frame=... ingame=1 area=03 room=01 x=... y=...
tools/tmc-link/tmc-link.py press LEFT 120       # hold LEFT 120 logic frames and wait for them
tools/tmc-link/tmc-link.py keys A+B 2           # buttons by their 3DS names, joined by +
tools/tmc-link/tmc-link.py touch 160 120        # a bottom-screen tap
tools/tmc-link/tmc-link.py savestate            # a new save state; `savestate 2` saves over number 2
tools/tmc-link/tmc-link.py loadstate            # the newest save state; `loadstate 3` loads number 3
tools/tmc-link/tmc-link.py states               # number=area/room for each
tools/tmc-link/tmc-link.py shot                 # /tmp/tmc-shot/top.png and bottom.png
tools/tmc-link/tmc-link.py peek gRoomControls 16    # symbols from build-3ds/game/tmc-3ds.elf
tools/tmc-link/tmc-link.py --elf ../wt/build-3ds/game/tmc-3ds.elf peek gHUD 8   # a build made elsewhere
tools/tmc-link/tmc-link.py switch "FRAME SYNC"
tools/tmc-link/tmc-link.py gpucheck             # next frame by both renderers: differing=N structural=N
tools/tmc-link/tmc-link.py perf start / perf stop [DIR]   # the perf recorder; stop fetches the CSV
tools/tmc-link/tmc-link.py poke gPpuGpu3DSRingDisable 0f   # bit per BG: draw it from quads again (00 = rings on)
tools/tmc-link/tmc-link.py log                  # flush (MARK) and fetch the newest SD log
tools/tmc-link/tmc-link.py run tools/tmc-link/examples/walk-left.txt
```

Symbols are looked up in the ELF of `build-3ds/` in this checkout unless `--elf` names another
(goes before the command, with `--ip`). It must be the ELF of the build the console is running:
a build made in another worktree has other addresses, and `peek`/`poke` by symbol would read and
write the wrong memory.

Buttons are added to the real ones and counted in logic frames, so a held direction
moves Link exactly as far each time. Touches, screenshots, dumps,
switches and writes run on the main thread between frames; the reply waits for them.
Reads and writes check that the memory is mapped first, so a wrong address answers
`550` instead of crashing. Together with the FTP server coming back after a relaunch,
`./build_3ds.sh --install IP && tools/tmc-link/tmc-link.py run SCRIPT` installs a build
and plays a scenario on it with nobody at the console.

## SD log

`debug/tmc-log-NNNN.txt` in the data folder. Debug builds start it at boot (one
file per session; the SD LOG cell stops and restarts it, each start is a new
file). It holds the version, the console, core 1, kernel and free memory, every
setting (`settings:` lines, the keys and values of `tmc3ds.ini`), then each change as
it is saved (`setting: frame_pacing 0 -> 1`: OPTIONS, the bottom-screen settings and
the SWITCHES alike), the marks, and every 5 s a perf line: frames shown and logic ticks
per second, presentations skipped, engine work per logic tick (and its max),
vblank wait per frame, GPU frames and software fallbacks, tiles decoded, area and
room, free memory; with it, while the GPU renderer draws, a `build:` line with the command
build's phases in ms per GPU frame (`sig` and `retain` are inside `maps`; `decode` is inside
the phase that needed the tile). `ring digest` and `cells` (inside `maps`) are the view rings'
change detection and cell updates. A `scene:` line follows: bands, vertices and quads of the last
GPU frame, `ring why` per BG (0: drawn from its ring; 1 not wide / disabled, 2 message or HUD+shadow,
3 control or mosaic or size, 4 bands disagree on the scroll column, 5 too many rows, 6 and 7 a ring
could not be brought up to date), and the cells decoded and changed character chunks since boot. A `bottom:` line
gives the bottom screen's paints in those 5 s, their paint time (wall time on the painter thread, so it
grows when that thread is starved) and the wait from a paint being scheduled to being submitted: what a
tap or a HUD change waits for. BUFFERED (default): lines collect in a 16 KB buffer, written when
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
| `decodes`, `map_rebuilds`, `map_refreshes` | GPU path, this frame only: tiles decoded into the atlas, BG layers whose quads were re-emitted, BG layers kept with their tiles re-decoded in place |
| `why_new` .. `why_window` | the map rebuilds above by reason (new, tilemap, palette, tiles, window) |
| `vertices` | vertices emitted for the frame |
| `ph_*_ms` | the builder's phase timers (maps, map signature, map retain, bg, obj, tile decode, ring digest, ring cells), as the SD log's `build:` line but per frame; the build has `PPU_GPU3DS_PROFILE` |
| `ring_why0..3` | per BG: why its view ring was not used this frame (0: it was; 1 unavailable, 2 dialogue shift or right-anchored HUD) |
| `ring_cells`, `ring_memo_hits` | ring cells brought up to date this frame, and how many of them were copied from an identical cell decoded earlier in the frame instead of decoded |

On the PC: `tools/perf-csv/analyze.py FILE.csv [--worst N] [--by-room]` prints the
frame rate, interval percentiles, missed vblanks, the mean / p95 / max of each part
and the worst frames with their parts.

## Frame recorder (SITE REC)

For a glitch that lasts a few frames at a room change (on the Old 3DS the game drops to ~20 fps
while it is armed: tell the owner if they are playing). `tmc-link.py rec` arms it: it keeps the
last 24 frames of the top screen as shown (200x120), and when the room changes (or the HUD is hidden) records 36
more and writes `debug/frames.bin`. A change of the HUD's hide flags (a cutscene taking the
buttons away) starts the recording as well. `tmc-link.py recfetch [DIR]` waits for the file, deletes
it on the console and writes `DIR/frames.png`, a contact sheet of the 60 frames, and prints
each frame's view size, Full View mode and whether the top screen was being held. Read the
sheet left to right, top to bottom.

## Scene recorder (SITE SCENEREC)

For a video of the top screen, full size (PLAN P1.6, after `../sm-3ds`): `tmc-link.py scenerec start [N]`
keeps one presented frame in N (default 2: 30 fps) in a RAM ring as large as free memory allows
with 8 MB left over (on the 2DS in a game about 90 frames, 3 s at N = 2; the New 3DS holds more);
`tmc-link.py scenerec stop [DIR]` writes the ring to `debug/tmc-scene-NNNN.bin` at the next frame
(the game stops while it does) and fetches it to DIR. `tools/scene-rec/decode.py REC.bin [OUTDIR]
[--gif OUT.gif] [--first N] [--last N] [--crop X,Y,W,H] [--scale N]` writes the frames as PNG and,
with `--gif`, an animated GIF at the rate they were taken. The file: a header (`SCN1`, frame count,
400, 240, N), then per frame its number and 400x240 RGB565 in the framebuffer's order (columns from
the left, each bottom to top). A capture costs a few ms a recorded frame.

## Screen dump

DEBUG -> SCREEN DUMP, or `tmc-link.py dump`. It does not write at once: the request is
taken at the next frame boundary (`Platform3DS_BeginFrameBoundary`'s wait), where the game
is paused for the SD writes and `DUMP SAVED` shows, as in ../sm-3ds and ../mzm. Each dump
takes the number after the last one (`debug/tmc-dump-last.txt`), is never overwritten and
has no limit; clear them out by hand. Files, all `debug/tmc-dump-NNNN-<name>`:

| Name | What |
|---|---|
| `top-screen.bmp/.raw`, `bottom-screen.bmp/.raw` | what both screens show |
| `ewram.bin`, `iwram.bin`, `vram.bin`, `palettes.bin`, `oam.bin`, `io-registers.bin` | the GBA memories |
| `io-per-line.bin`, `dispcnt-per-line.bin`, `affine-ref.bin` | the GPU builder's per-scanline registers (GPU frames) |
| `main-state.bin`, `room-controls.bin`, `map-*.bin`, `bg*-buffer.bin`, `save-state.bin` | the game's state |
| `gpu-frame.bin`, `parity-cpu.bin`, `parity-gpu.bin`, `parity-diff.txt` | the frame as the GPU drew it and, if it differs from the CPU's, both surfaces |
| `info.txt` | build, view and Full View state, frame cadence, per-core PPU timings, GPU work, audio, memory, input |

A dump pauses audio and writes a few MB. `SITE INSTALL` is refused for 15 s after one
(`450`): relaunching right after it made the system loader crash once (issue #4).

Upstream's L+R+A and its LOAD STATE of the last dump are gone. A game state is restored by
the STATES tab (docs/save-states.md); `tmc-link.py shot/peek/poke/state` cover the rest.
The files' content is what `tools/` and `platform/3ds/tests/port_ppu_gpu_3ds_bench.c` read;
the bench takes a dump's files by their old unprefixed names, rename or copy them.

## Reading a Luma crash dump

A crash on the console leaves `sdmc:/luma/dumps/arm11/crash_dump_NNNNNNNN.dmp` (the highest number is
the newest; reachable over the game's FTP, `/luma/dumps/arm11/`). The format, as Luma3DS writes it
(checked on dumps 14 to 16):

| Offset | What |
|---|---|
| 0 | magic `0xDEADC0DE 0xDEADCAFE`, then `u16 major, minor` (3, 1) |
| 12 | `u32` processor (11 = ARM11), `u32` the exception type, `u32` total size, `u32` register dump size (0x5c), `u32` code dump size (0x60), `u32` stack dump size, `u32` additional data size |
| 40 | registers: r0 to r12, sp, lr, pc, cpsr, fpexc, fpinst, fpinst2, dfsr, ifsr, far (23 words) |
| 40 + 0x5c | the code around pc, then the stack from sp (`stack dump size` bytes), then the additional data (the process name) |

Steps: (1) the SD log's header (`debug/tmc-log-last.txt`, then `tmc-log-NNNN.txt`) names the
build, e.g. `v0.3.0-dev.42.1+933b3f8`; (2) the symbols must be that build's ELF, not the working
tree's: check out that commit (a detached HEAD, stashing what is uncommitted), `./build_3ds.sh
--mode debug --no-ftp`, keep `build-3ds/game/tmc-3ds.elf`; (3) `arm-none-eabi-addr2line -f -C -e
tmc-3ds.elf <pc>` for the faulting address, and the same over every stack word that falls in
`0x100000..0x640000` for the call chain (the real return addresses are the ones that make sense as a
chain; `lr` is often garbage). A `pc` in `memset` with `r0 = 0` is a clear of a NULL pointer;
`far` is the data address of a data abort. A small Python script that does the parsing is two
dozen lines (`struct.unpack_from('<23I', d, 40)`).

## On the PC

`tools/ui-preview/build.sh [OUTDIR]` renders the bottom screen's tabs to PNG
without a console or a ROM (`TMC3DS_DEBUG_TOOLS=0` for a production build's view).
Without a ROM the map and item art take their fallbacks; the OPTIONS and DEBUG
tabs draw exactly as on the console (with stand-in values for the log, recorder
and switches). `THEME=n` picks the theme. The RetroAchievements tab shows a sample set
(`RA_OFF=1`: logged out; `RA_TOAST=1`: the unlock notice on the bottom screen). The shots and the taps that reach them are
the `SHOTS` list in the script.

## Screen fades

A uniform fade is applied by the GPU as one blend over the finished picture (`docs/gpu-fade.md`);
`gpu_fade=0` in `tmc3ds.ini` turns it off. Over the link: `peek gFadeGpu3DSStats 64`
(counters, 16 words) and `poke gPpuGpu3DSFadeQuadDisable 01` (skip the blend, to time it).
Parity failures print `parity: ... fade=<weight>/<level>` to the SD log.
