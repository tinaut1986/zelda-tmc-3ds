# Minish Cap 3DS (tinaut1986 fork): action plan

Living document. Read it at the start of every session; keep it current as things
change (what goes where: the table in `CLAUDE.md`).

- **Goal:** The Minish Cap on 3DS running **smoothly at 60 fps on both New 3DS and
  Old 3DS/2DS** (no stereo 3D on either), with a **wide mode that fills the 400x240
  top screen** both pixel perfect and scaled, by showing more of the world than the
  GBA did; debug tools and a bottom-screen UI on par with `../mzm` and `../sm-3ds`.
- **Starting point:** EstebanPdN's `zelda-tmc-3ds` v2.2-E1 (forked 2026-10-11). On
  the owner's consoles it is not smooth on either model, even with the 3D slider
  down. It already has a GPU tile renderer (`platform/3ds/source/port_ppu_gpu_3ds*.c`,
  on by default) with a software fallback, an Old 3DS adaptive frame pacer, NDSP
  audio offload, an updater, a quick memory dump (L+R+A) and an experimental New 3DS
  "Full View" (Wide + Pixel Perfect: 400x240 outdoors, 200x120 at 2x indoors).

## Status (2026-10-11)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.1.1` (on origin). Published: **`v0.1.0`** (2026-10-11, **beta**,
not on `main`; GitHub release with the CIA and QR, the 3DSX asset deleted afterwards): the fork
setup, the compact bottom screen with themes, FRAMES, the DEBUG tab tools and the GPU renderer on
both models (branches `chore/fork-setup`, `feat/compact-ui`, `feat/debug-tools`, merged into
`release/v0.1.0` before the tag; none of it checked on the console yet). `main` is still upstream's
v2.2-E1: nothing stable published.

**Branches:**

| Branch | What | State |
|---|---|---|
| `chore/no-3dsx-release` (cut from `release/v0.1.1`) | releases publish the CIA only | ready; not merged |
| `feat/ingame-ftp` (cut from `chore/no-3dsx-release`) | P1.9 FTP server in the DEBUG tab, `SITE INSTALL` + relaunch, `./build_3ds.sh --install`, CLAUDE.md "The console in the loop", PLAN P1.10 spec | checked on the PC with curl; not merged |

**On the owner's console now (2026-10-11):** a debug build of `feat/ingame-ftp`
(`0.1.1-dev.13.4+6528614`), sent with ftpd and installed with FBI: the first time any of this
fork runs on hardware. To check, in order: it boots from `sdmc:/3ds/Minish Cap 3DS/`; the GPU
renderer on the New 3DS draws correctly (Wide normal, then Wide + Pixel Perfect); the bottom
screen (tabs, themes, OPTIONS, the title-screen OPTIONS); DEBUG -> FTP SERVER, then
`./build_3ds.sh --install IP` from here; perf recordings in Hyrule Town on the New 3DS (Wide,
and Wide + Pixel Perfect) and later on the 2DS (P0.2).

**Priority** (owner's order, 2026-10-11): P0.1 → P1 debug tools and P2 bottom UI (so
that P3 can be measured) → P0.2 baseline → P3.4 Full View on the GPU, and Full View on the
Old 3DS (P4.1) → the rest of P3 → P4.2.

Tasks ticked [x] have been checked on the console by the owner; do not re-propose them.

## How work is tracked

- **This file** is the roadmap: phases, task specs with acceptance criteria,
  decisions, and a short Status (open branches, priority). It is the source of
  truth for "what next" and is what a new Claude session reads.
- **GitHub issues** on `tinaut1986/zelda-tmc-3ds` are for bugs found by playing:
  things with a repro, screenshots, a console model. Link the issue from the task
  here when a task spawns from one; do not duplicate specs into issues.
- Larger design work gets its own note in `docs/` and is linked from its task.

Task format: `- [ ] **ID** title`, then *Spec* (what) and *Done when* (verifiable
criteria). IDs are stable; never renumber.

---

## Known facts about the upstream code (verified 2026-10-11)

- Upstream history is squashed: 7 commits, one per release (v1.0 ... v2.2-E1).
  Its tags have two components and stay in this repository.
- Build: CMake (`platform/3ds/CMakeLists.txt`), ~1.5 min for a full build. The
  PC/Android builds use `xmake.lua`; `port/` is shared with them.
- Rendering: the game draws through a software GBA PPU model (`port/ppu/`,
  `mode1.c`) whose state the GPU renderer (`port_ppu_gpu_3ds.c`, tile atlas,
  batches, stencil, a parity check against the software output) turns into
  citro3d draws; heavy frames fall back to the software path (~15 ms). The
  presenter (`platform_gpu_3ds.c`) scales to the top screen by display style
  (Blur, Bilinear, Pixel Perfect) and aspect (Wide, Original, Stretch).
- Many performance switches exist only in `tmc3ds.ini` (`port_config_3ds.c`):
  `gpu_renderer`, `gpu_frame_sync`, `bottom_core`, `audio_core`, `audio_dsp`,
  `audio_dsp_pcm`, `bottom_map_skip`, `vblank_phase_lock`, `gpu_static_quad`,
  `compact_upload`, `bottom_rgb565`, `gpu_short_vertices`, `app_cpu_limit`,
  `frame_log`... Several default off. No on-console way to change them.
- Debug today: the Developer submenu (VERSION/MODEL/FPS/CORE1/SCREEN/AREA/ROOM
  overlay, MEM DUMP, LOAD STATE of the last dump) and L+R+A (pause + quick dump to
  `dumps/dump-*`: framebuffers, memories, timings). No SD log, perf recorder, scene
  recorder, warp, cheats or save states.
- The bottom-screen compositor (`port/port_second_screen.c`, ~3.5k lines) comes
  from the Android second screen: every metric is `u = min(w,h)/720`, so on the
  3DS's 320x240 everything is a third of a 720p design. Text is the game's banner
  font at 16 px boxes or a 5x7 fallback scaled up; the tab bar takes 32 px and the
  sidebar 73 px. That is why it looks crowded and fits few options.
- `docs/OLD3DS-HANDOFF.md` and `docs/old3ds-pica200-parity-notes.md` are Project
  Picori's Old 3DS notes (Aug 2026): 53 fps on Old 3DS, audio render 0.84 ms/buffer,
  presentation max 196 ms spikes. Useful history, not current measurements.

## Reuse map from `../mzm` and `../sm-3ds`

| Area | Source | Reuse |
|---|---|---|
| Version from git, channel baked in, release CI, release notes, updater rules | `../sm-3ds` Makefile, `.github/workflows/build-release.yml`, CLAUDE.md | Done in P0.1 (CMake + `tools/version.sh` instead of make) |
| Build assistant with LAN FTP scan | `../sm-3ds/tools/build_3ds.py` | Done in P0.1 |
| DEBUG tab, tools window, report window | `../sm-3ds/source/bottom_ui.c`, `docs/debug-tools.md`; `../mzm/docs/3ds-debug-tools.md` | Adapt: same cells and files, TMC data |
| SD log (buffered/direct), perf recorder CSV + analyser | `../sm-3ds` `debug_log.c`, `perf_rec.c`, `tools/perf-csv/analyze.py` | Adapt columns to this renderer |
| Scene recorder + decoder | `../sm-3ds` `scene_rec.c`, `tools/scene-rec/decode.py` | As is (400x240 RGB565 RLE) |
| Bottom UI look: cell grid, 5x7 font at 1x, slim icon tab bar, status bar | `../sm-3ds/source/ui_draw.c`, `ui_font.c`, `bottom_ui.c` | Adapt: keep TMC's map/items/quest content |
| UI preview on the host | `../sm-3ds/tools/ui-preview/` | Adapt: build `port_second_screen*.c` on the host |
| Save states | `../sm-3ds` STATES tab, `../mzm/platform/3ds/source/port_save_state.c` | Idea; TMC has `port_dump_state.c` (load of a dump) to start from |
| Perf method (measure on hardware, CSV, one change at a time) | `../mzm/docs/3ds-renderer-perf-plan.md`, `../sm-3ds` PLAN P2.x | Method |

---

## Phase 0: fork foundation

- [ ] **P0.1** Own the fork: versions, app identity, releases.
  *Spec:* version from git (`vX.Y.Z` tags only, `X.Y.Z-dev.N+hash` on release
  branches), upstream base in `platform/3ds/upstream_version.txt` and shown next to
  ours; own title ID / name / data folder so it installs beside upstream's app;
  updater pointed at this repository, aware of `-beta` / `-dev`; release workflow
  (tag → CIA + 3DSX + QR, Beta/Release by reachability from `main`, debug tools in
  betas, notes block for the updater); compile-check workflow; build assistant with
  FTP; CLAUDE.md and this plan.
  *Done when:* the owner installs a CIA built from `release/v0.1.0` next to
  upstream's app, it boots from `sdmc:/3ds/Minish Cap 3DS/` and shows
  `v0.1.0-dev...` (upstream 2.2-E1); a first beta tag publishes a release whose CIA
  the console's updater sees.
- [ ] **P0.2** Baseline measurements.
  *Spec:* with the P1 perf recorder, record fixed spots (title screen, Hyrule Town,
  Minish Woods, Deepwood Shrine, Mt. Crenel, Castor Wilds, Cave of Flames, a busy
  boss) on New 3DS and Old 3DS/2DS, Wide + Pixel Perfect and Original, 3D off.
  *Done when:* a table below with fps (avg / 1% low), CPU and GPU ms per frame.
- [x] **P0.3** Issues enabled on `tinaut1986/zelda-tmc-3ds`.
  Done 2026-10-11 (`gh repo edit --enable-issues`, at the owner's request).
  *Spec:* the fork has issues disabled (GitHub's default for forks). The owner
  enables them in Settings, or asks Claude to (`gh repo edit --enable-issues`).
  *Done when:* playtest bugs can be filed there.

## Phase 1: debug tools (DEBUG_TOOLS builds)

All of these exist only when `TMC3DS_DEBUG_TOOLS=1`; documented in
`docs/debug-tools.md` as they land. Files go to `debug/` in the data folder.

- [ ] **P1.1** DEBUG tab and the DEBUG_TOOLS switch.
  *Spec:* `TMC3DS_DEBUG_TOOLS` (generated `tmc3ds_build_config.h`) compiles in a
  DEBUG tab on the bottom screen: live timings (fps shown/engine, CPU ms for
  logic / PPU / GPU build / submit / present / bottom paint / audio), game state
  (area, room, Link x/y, region), and a DEBUG TOOLS window listing the tools below.
  The existing Developer overlay and MEM DUMP move here; L+R+A stays.
  *Done when:* a debug build shows the tab, a production build has no trace of it
  (no strings in the binary).
- [ ] **P1.2** Live performance switches.
  *Spec:* every ini-only performance switch gets a cell in DEBUG -> SWITCHES, showing the
  current value, applied during play (none may need a restart: owner's rule, 2026-10-11) and
  saved to the ini. Done in `v0.1.0`: renderer, frame sync, vblank lock, static quad, map skip,
  frame log, bottom core, audio core, compact upload, CPU limit (docs/debug-tools.md).
  *Done when:* each can be flipped on the console and the perf recorder shows the difference.
- [ ] **P1.3** SD log.
  *Spec:* `debug/tmc-log-NN.txt` (10 rotating slots), started at boot in debug
  builds: console model, clocks, core-1 limit, every settings change, every 5 s a
  perf line (fps, work/logic/ppu/gpu/present ms, fallbacks, audio health), exit
  steps. BUFFERED (16 KB, flushed on mark/exit) or DIRECT. LOG MARK writes a marker.
  *Done when:* a session's log can be read after closing the game.
- [ ] **P1.4** Perf recorder + analyser.
  *Spec:* `debug/tmc-perf-NN.csv`, one row per engine frame (columns modelled on
  `../sm-3ds` P2: logic, ppu, gpu build/submit/wait, present, bottom, audio, shown,
  skipped, fallback reason, area/room); `tools/perf-csv/analyze.py` prints fps,
  percentiles, and what the frames over budget were doing.
  *Done when:* a recording from each console model is analysed on the PC.
- [ ] **P1.5** Report window and screen/frame dumps.
  *Spec:* SCREEN DUMP (top + bottom framebuffers, VRAM, palettes, OAM, IO, game
  state) with the reason window from `../sm-3ds` (generic reasons, keyboard text,
  CANCEL) writing `-note.txt`; dumps numbered with a `last.txt` counter (never
  overwritten). GPU CHECK draws one frame with both renderers and saves both.
  *Done when:* a dump with a note and a GPU CHECK pair are fetched over FTP.
- [ ] **P1.6** Scene recorder.
  *Spec:* RAM ring of the shown top screen (60/30/15 Hz), written on stop as
  `tmc-rec-NNNN.bin` (sm-3ds format); `tools/scene-rec/decode.py` → PNG/mp4.
  *Done when:* a few seconds of play decode to a video on the PC.
- [ ] **P1.7** Warp and cheats for testing.
  *Spec:* teleport to any area/room (list by area name), refill hearts / max
  rupees / all items toggles, invincibility; a `CHT` mark on the top bar while on.
  *Done when:* any dungeon can be reached in seconds for a perf or bug check.
- [ ] **P1.9** FTP server inside the game (owner's request, 2026-10-11).
  *Spec:* DEBUG -> FTP SERVER: anonymous, passive, SD root as `/`, port 5000, while the
  game runs; shares the socket service with the updater.
  *Done when:* the debug folder is fetched from a PC while playing, and a file uploaded.
  Status: protocol checked on the PC with curl (list, get, put, mkdir, delete, 5 MB both
  ways); waiting for the console. Also SITE INSTALL: install a CIA over the running game and
  relaunch (`./build_3ds.sh --install`).
- [ ] **P1.10** Live debug link for Claude (owner's request, 2026-10-11).
  *Spec:* Claude, on the owner's PC on the same LAN, looks at and acts on the running game:
  1. A TCP command server in debug builds (DEBUG -> LINK, like FTP SERVER) with a PC client
     `tools/tmc-link/` usable from a shell: screenshot of both screens as PNG, read and write
     GBA memory (EWRAM, IWRAM, VRAM, palettes, OAM, I/O) and the port's state while it runs,
     game state (task, area, room, Link, health, inventory), set any OPTIONS / SWITCHES value,
     inject buttons and touches for N frames, pause / step N frames / resume, warp.
  2. Symbols: read and write variables by name (`gRoomControls.area`) from the ELF's symbol
     table on the PC side, so no address is typed by hand.
  3. Live tunables: a small table of named values the game reads every frame, for numbers being
     tuned (sizes, offsets, timings), changed from the client without rebuilding.
  4. For breakpoints and stepping through code: Luma3DS's own GDB stub (Rosalina -> Debugger)
     with `arm-none-eabi-gdb build-3ds/game/tmc-3ds.elf`; documented, no code of ours.
  Code changes are not hot-patched: they go through `./build_3ds.sh --install` (P1.9), about
  two minutes from edit to running.
  *Done when:* Claude, from a shell here, takes a screenshot, reads Link's position, changes a
  switch and injects a button press on the owner's console, and the owner sees it happen.
- [ ] **P1.8** Save states.
  *Spec:* numbered states with thumbnail and place (like `../sm-3ds` STATES),
  available in every build; builds on `port_dump_state.c`.
  *Done when:* save, power cycle, load in another area works on both models.

## Phase 2: bottom-screen UI

- [ ] **P2.1** UI preview on the host.
  *Spec:* `tools/ui-preview/` builds `port_second_screen*.c` for the PC with a fake
  snapshot and writes each tab/page as PNG at 320x240 (and 2x), without a ROM
  (theme fallbacks) and with one when given.
  *Done when:* every tab renders to PNG in one command.
- [ ] **P2.2** Compact layout.
  *Spec:* a layout for 320x240 instead of the 720p one scaled down: slim icon tab
  bar (~20 px) with a status strip (clock, battery, CHT/REC marks), 5x7 font at 1x
  for labels and values, settings as a grid of two-line cells (name over value)
  like `../sm-3ds` OPTIONS, map and items keeping the game's art but with less
  padding. Every current setting stays reachable; the version + upstream base
  show in the footer.
  *Done when:* the owner approves previews, then checks it on the console.
- [ ] **P2.3** OPTIONS regrouped.
  *Spec:* SCREEN (aspect/wide mode, style, HUD, colour correction, backdrop),
  GAME (turbo, volume, autosave, text, map follow, crests, floor return),
  SYSTEM (updates channel/check/what's new, CPU clock on New 3DS, FPS counter,
  reset). Randomizer row removed while it does nothing.
  *Done when:* checked on the console.

## Phase 3: performance (60 fps, New 3DS and Old 3DS, no 3D)

Measured with P1 tools, one change at a time, on hardware; numbers in the table
below and the decisions log.

- [ ] **P3.1** Find where the frame goes on each model (from P0.2): logic vs PPU
  model vs GPU build/submit vs present vs bottom screen vs audio, and how often the
  software fallback runs.
- [ ] **P3.2** New 3DS at a locked 60 in every P0.2 spot, Wide + Pixel Perfect.
- [ ] **P3.4** Everything through the GPU.
  *Spec:* owner's rule (2026-10-11): every frame is drawn by the GPU renderer on both models;
  the software PPU stays only as a comparison (DEBUG -> SWITCHES -> RENDERER, GPU CHECK). Done
  so far: the renderer runs on the New 3DS too (upstream kept it to the Old 3DS). Left: the
  frames that still fall back to software, each to be taught to the GPU renderer: the New 3DS
  Full View layouts (400x240 outdoors, 200x120 at 2x indoors), command-budget overflows,
  build failures (`PpuGpu3DSBuildReason`). The perf recorder's `gpu` column and the SD log's
  `fallback` count show how many are left.
  *Done when:* a long session on each model records `gpu` = 1 on every gameplay frame.
- [ ] **P3.3** Old 3DS/2DS at 60 (or a stable 30 with no hitches where 60 is out of
  reach, decided with the owner) in every P0.2 spot.

## Phase 4: wide modes

- [ ] **P4.1** Wide pixel perfect on both models.
  *Spec:* the game view is 400x240 logical pixels shown 1:1 (no scaling), showing
  more of the room than the GBA's 240x160; camera clamping, room edges, entity
  culling and spawning extended so nothing pops in or is cut. Builds on upstream's
  Full View (New 3DS only, outdoors only) and `docs/widescreen-*.md`.
  *Done when:* every area plays in it on both models with no visual or logic bug
  known.
- [ ] **P4.2** Wide scaled on both models.
  *Spec:* a wider logical view (e.g. 266x160, 16:9 at 1.5x) scaled to fill 400x240,
  with the same extended world as P4.1.
  *Done when:* as P4.1.

---

## Baseline measurements (P0.2)

Owner's report on upstream v2.2-E1 (2026-10-11, by eye, before the P1 tools):

| Console | Setting | What it showed | fps |
|---|---|---|---|
| New 3DS | Wide + Pixel Perfect (Full View) | 400x240 view, more world | ~30 |
| Old 3DS | Wide + Pixel Perfect | the same picture as Original (Full View is New 3DS only in upstream's policy) | ~30 |

Reference: `../mzm` runs wide + pixel perfect **with stereo 3D** at or near 60 on both models.
Reading: on the New 3DS, Full View is drawn by the software PPU (2.5x the GBA's pixels), which
P3.4 moves to the GPU; the Old 3DS at ~30 without Full View means something besides drawing
(pacing, bottom screen, GPU waits) to be found with the perf recorder.

## Decisions log

- **2026-10-11** Own version line `vX.Y.Z` from `v0.1.0` (owner's choice), separate
  from upstream's `vX.Y[-En]`. Three components keep the two apart without deleting
  upstream's tags; the upstream base is shown alongside.
- **2026-10-11** The fork installs as its own app (title ID `0x5A22E`, "Minish Cap
  3DS", `sdmc:/3ds/Minish Cap 3DS/`), owner's choice: both can be installed, and our
  config changes cannot break upstream's install. The updater checks the downloaded
  CIA's title ID against ours.
- **2026-10-11** Workflow copied from `../sm-3ds` (spec-driven PLAN, issues for
  bugs, `release/*` + `main`, betas carry debug tools). CMake stays (no switch to
  make); `version.sh` does what sm-3ds's Makefile does.
- **2026-10-11** Bottom-screen themes: four kept (SLATE, PARCHMENT, NIGHT GOLD, FOREST with
  the tabs below), switchable on the console; the owner will tune them after using them. Each
  theme sets its own panel backdrop when picked.
- **2026-10-11** The repository stays public for now: GitHub does not let a fork of a public
  repository be made private, and a private repository would break the console's updater and
  the release QR codes (both download anonymously). Asked the owner before doing anything.
- **2026-10-11** The GPU renderer on both models (owner: "everything, always, through the GPU;
  if it takes more work, it takes more work"). Upstream's New 3DS drew in software on several
  cores. Full View frames still use the software PPU until P3.4 teaches the GPU renderer them.
  Context from the owner: upstream's GPU path was built as a stopgap for Old 3DS complaints
  about the software renderer, not as the intended design; here it is the main renderer, as
  in `../mzm` and `../sm-3ds`, and the software PPU is only a reference.
- **2026-10-11** Every DEBUG switch applies during play (owner asked why any needed a restart):
  CPU limit through APT at once, the bottom painter and audio threads recreated on the other
  core, the compact upload layout swapped at a frame start (the surfaces are now allocated at
  full size). `bottom_rgb565` dropped from the page: upstream documents it as broken.
- **2026-10-11** OPTIONS -> FRAMES (owner's request, as in the Metroid ports): AUTO, LOCK 30,
  NO SKIP (`frame_pacing`). AUTO uses upstream's Old 3DS adaptive pacer **on the New 3DS too**,
  which upstream never did (it always drew every frame there): to be judged on the console; NO
  SKIP is upstream's old New 3DS behaviour. LOCK 30 waits a vblank on the undrawn tick so the
  game stays at 60 Hz. C-stick turbo on the New 3DS is unchanged and overrides it while held.
- **2026-10-11** The updater shows the `<!-- tmc-notes -->` block (our release
  notes) and falls back to upstream's `## Changelog` section.
