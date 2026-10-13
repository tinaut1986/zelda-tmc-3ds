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
  "Full View" (Wide + Pixel Perfect: 400x240 outdoors, 200x120 at 2x indoors; the 2x indoor zoom is gone, see the decisions log).

## Status (2026-10-13)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.1.2` (on origin). Published: **`v0.1.1`** (2026-10-12, **stable**,
merged into `main`) and `v0.1.0` (2026-10-11, beta).

**Branches:**

| Branch | What | State |
|---|---|---|
| `chore/no-3dsx-release` (cut from `release/v0.1.1`) | releases publish the CIA only | ready; not merged |
| `feat/ingame-ftp` (cut from `chore/no-3dsx-release`) | P1.9 FTP server in the DEBUG tab, `SITE INSTALL` + relaunch, `./build_3ds.sh --install`, CLAUDE.md "The console in the loop", PLAN P1.10 spec | checked on the PC with curl; not merged |
| `feat/log-settings` (cut from `feat/ingame-ftp`) | the SD log records every setting at start and each change as it is saved | builds; not checked on the console |
| `perf/gpu-build-profile` (cut from `feat/log-settings`) | SD log `build:` line (the GPU builder's phase timers); `docs/renderer-gpu-gap.md` | builds; to record in Hyrule Field |
| `feat/debug-restore` (cut from `perf/gpu-build-profile`) | the SD log (on/off, mode) and the FTP server come back at launch as left; debug builds only | builds (debug and production); not checked on the console |
| `feat/bottom-always` (cut from `feat/debug-restore`) | the whole bottom screen outside gameplay too (intro, title, file select): every tab, OPTIONS and DEBUG working, MAP/ITEMS/QUEST say they need a game | builds; not checked on the console |
| `feat/remote-link` (cut from `feat/bottom-always`) | P1.10 first cut: SITE KEYS/TOUCH/SHOT/LOADSTATE/DUMP/SWITCH/STATE/PEEK/POKE over the FTP server, `tools/tmc-link/` | on the console (2026-10-11): the FTP server came back by itself after each install; STATE, SHOT (2 s) and KEYS work (START at the title reached file select, read back in `gInput`); SWITCH, TOUCH, LOADSTATE, POKE not tried yet |
| `feat/save-states` (cut from `feat/remote-link`) | P1.8 save states (STATES tab, `3dsx.ld` game layout pinned, link SAVESTATE/LOADSTATE), tab-bar theme button removed | on the console (2026-10-11, by Claude over the link): save 2 s, load 0.3 s, exact; a state loaded from the title right after a relaunch, and after a rebuild that moved the port's code by 0x50 the game went on normally. The tab itself (cards, detail, confirm) not yet seen by the owner |
| `fix/gpu-wide-shadow-stride` (cut from `feat/save-states`) | normal Wide on the GPU renderer: garbage in the right-hand columns (shadow rows read at the wrong stride) | fixed and checked on the console by Claude (two save states in Hyrule Field 03/01, against the software renderer) |
| `perf/wide-bg-tile-runs` (cut from `fix/gpu-wide-shadow-stride`) | Wide BG build: tile runs checked at their ends instead of every pixel; link MARK and GPUCHECK | on the console by Claude: Hyrule Field 03/01 Wide + Bilinear, bg 10.4 -> 4.9 ms, 57.5 -> 59.8 fps with 10.7 ms of vblank margin; GPU CHECK identical to the build before (5176 / 3986 differing pixels, 0 structural) |
| `perf/full-view-gpu` (cut from `perf/wide-bg-tile-runs`) | P3.4 first part: Full View (Wide + Pixel Perfect) drawn by the GPU renderer: whole-view shadow, OBJ Y wrap as mode1.c, presenter takes the Full View geometry | on the console by Claude, Hyrule Field 03/01: Full View 400x240 33.8 -> **59.8 fps** (bg 6.2 ms, vblank margin 9.6 ms; the per-frame tile memo and the per-tile Wide emitter), Wide + Bilinear bg 4.2 ms; GPU CHECK 0 structural pixels everywhere tried. Interiors (200x120 at 2x), BG3 native bounds and the Old 3DS not checked yet |
| `fix/relaunch-hang` (cut from `perf/full-view-gpu`) | issue #2: no GPU teardown while relaunching, exit steps logged | relaunches checked; the hang itself not reproduced |
| `perf/full-view-old3ds` (cut from `release/v0.1.1`) | P3.3/P4.1: Full View on the Old 3DS (the New 3DS gate is gone) and the view rings: each BG is a texture of decoded tiles, drawn as one quad a band (`docs/renderer-gpu-gap.md`) | on the 2DS by Claude, Hyrule Town 03/01: Full View 400x240 **59.8 fps walking, vblank margin ~9-10 ms** (it was 31 fps, build 22 ms; now 3 ms); host model test passes; not checked on the New 3DS or by the owner; **uncommitted** |
| `chore/debug-sm3ds-style` (cut from `perf/full-view-old3ds`, needs its c3fbc8f..9dbf7cd for the console) | upstream's debug options out, sm-3ds style in: L+R+A gone (debug builds only now), SCREEN DUMP numbered `debug/tmc-dump-NNNN-*` and deferred to the frame boundary, LOAD STATE and `port_dump_state_3ds.c` out (STATES tab), GPU CHECK writes `tmc-check-NNNN-*`, FRAME LOG and the TMC_3DS_DIAGNOSTICS blocks out, SITE INSTALL refused 15 s after a dump (issue #4) | builds (debug, production, UI preview); not installed on the console yet (installs paused while issue #4 is looked at) | _(merged into `release/v0.1.1` 2026-10-12: checked on the 2DS by Claude, build debug and production)_
| `perf/palette-anim-tile-cache` (cut from `perf/full-view-old3ds`) | issue #3, the dialogue fade-in in the castle garden (28 fps): ring cells copy an identical cell decoded earlier in the frame; **uniform screen fades on the GPU** (`docs/gpu-fade.md`: unfaded palettes + one blended quad, `src/` untouched so save states survive); perf recorder rows with the builder's counters | on the 2DS by Claude over the link: fade windows 28 -> 47-54 fps, no tile decoded during a fade (cells 0), 387/387 fades recognised; GPU CHECK still shows large structural differences on some fade frames (I=256, cause not found yet: the console went off the network before the new parity log line could be read). Not checked by the owner | _(merged into `release/v0.1.1` 2026-10-12: fade on the GPU checked on the 2DS, with fix/fullscreen-quad-corners)_
| `feat/port-ui-language` (cut from `release/v0.1.1`) | the port's own text (bottom screen, updater) follows the language chosen in-game: EN/FR/DE/ES/IT, `port/port_text_i18n.c`; accents fall back to the plain letter when the font lacks them | builds; not checked on the console (EU ROM: accents, widths of the longer FR/DE/ES/IT strings) |
| `fix/fullview-room-hold` (cut from `release/v0.1.2`) | Full View through the fade-in of an area change: no 266x160 frames before the 400x240 view, the fade-in from white visible again | on the 2DS by Claude over the link (castle garden <-> 03/06, both ways, frame recorder and view log) | _(merged into `release/v0.1.2` 2026-10-13: checked by the owner)_ |
| `fix/theme-contrast` (cut from `release/v0.1.2`) | bottom-screen themes readable: own colours for what sits on the tab bar (clock, battery, Wi-Fi, toast, busy window), PARCHMENT buttons light; `tools/ui-preview` builds again | on the 2DS by Claude (screenshots of MAP and STATES in PARCHMENT); the other themes only in `tools/ui-preview` | _(merged into `release/v0.1.2` 2026-10-13: checked by the owner)_ |
| `fix/ftp-stop-freeze` (cut from `release/v0.1.2`) | issue #9: stopping the FTP server froze the game (socExit with a client connected); stop, link waits and polls made safe, stop and watchdog logged in `tmc3ds.log` | on the 2DS by Claude and the owner (stop during a link request, stop with an idle client) | _(merged into `release/v0.1.2` 2026-10-13; the same fix merged into `../mzm` `release/v0.7.7` and `../sm-3ds` `release/v0.6.6`, not run in those games yet)_ |
| `fix/updater-audio` (cut from `release/v0.1.2`) | the update check broke up the sound on the Old 3DS: the updater worker one priority below the game thread instead of at the audio worker's | checked by the owner on the 2DS: no dropouts during a check | _(merged into `release/v0.1.2` 2026-10-13)_ |
| `feat/game-style-windows` (cut from `release/v0.1.2`) | the bottom screen's own menu style in every tab: cells, buttons and tabs with the game windows' rounded corners at 1x in the theme's colours, toasts / busy / modals in the message window's frame, the world map without the stone frame (its own box, Link's head as the marker), the quest lists' and wells' slab and stone through the same boxes (`Port_SecondScreenTheme_SetBoxPainter`) | on the 2DS by Claude (screenshots of every tab, both map views); the quest sub-lists (kinstones, techniques) not seen: the save on the console has neither | _(merged into `release/v0.1.2` 2026-10-13 on the owner's word)_ |
| `fix/fullview-border-scroll` (cut from `release/v0.1.2`) | issue #11: a room-border scroll keeps Full View (400x240 snapshot of the outgoing room, the scroll ends on the 400x240 view's edge, the view carried across the room change) instead of dropping to 266x160 | checked by the owner and by Claude on the 2DS, Hyrule Field 03/06 <-> 03/05 (right and left: every recorded frame 400x240); up and down not tried yet | _(merged into `release/v0.1.2` 2026-10-13)_ |
| `feat/sidebar-game-hud` (cut from `release/v0.1.2`) | P2.5: X/Y (and ZL/ZR) fire soft-slot items on 3DS; the sidebar's A/B/X/Y diamond with a selected button; R always shown with the game's label over it; rupees/keys in the HUD font | checked by the owner on the 2DS (X/Y with sword and shield: the only items the save has) | _(merged into `release/v0.1.2` 2026-10-13)_ |
| `fix/light-rays-full-view` (cut from `release/v0.1.2`) | the Minish Woods light rays in Full View: BG3 anchored to the view's right edge (they started mid-view and were cut at x=240 on the GPU renderer); BG3 in Full View drawn across the whole view by the GPU renderer as by the software one; BG3 HBlank effects in the native canvas only for Vaati's appearing | on the 2DS by Claude with the owner's state (00/00): rays enter from the right edge, GPU CHECK 0; in the combined build on the owner's 2DS |
| `feat/tabbar-position` (cut from `release/v0.1.2`) | P2.7: OPTIONS -> TABS (top / bottom) next to THEME; FOREST is a palette like the others; `tools/ui-preview` links again (soft-slot stubs) | on the 2DS by Claude (the cell shows TABS = TOP); the switch itself not tried yet |
| `feat/hud-xy-diamond` (cut from `release/v0.1.2`) | P2.6: X/Y in the top HUD, A/B/X/Y as a diamond; X/Y sprites drawn by the engine, their tiles in OBJ VRAM only while the frame renders | on the 2DS by Claude and the owner (combined build, Minish Woods 00/00): the diamond with sword on X and shield on Y, A/B slide into it from a state saved with the old layout, GPU CHECK 0 structural |
| `fix/chuchu-bad-child` (cut from `release/v0.1.2`) | a Chuchu no longer reads its FX child unless it is an entity: closing the pause menu after loading a state saved in it left `0x160` there and crashed (Luma dumps 2 and 3, `chuchu.c:84`); who writes the value is not known yet (issue #13) | on the 2DS by Claude: same state, no crash |
| `chore/tmc-link-elf` (cut from `release/v0.1.2`) | `tmc-link.py --elf`: symbols from the ELF of the build the console runs (a worktree's build has other addresses) | tried against the 2DS (peek by symbol with the combined build's ELF) |

**Where work stands (2026-10-12):** everything is merged into `release/v0.1.1` and pushed.
Latest, checked on the console by the owner and by Claude over the link: the boot screen (a
triforce turning on the bottom screen, the whole splash on top, no console text, no fixed delay),
the SHOW FPS box of four lines, save states from older builds (view and crash fixes, see the
decisions log) and taps on the tab bar. Still to check by the owner: the STATES tab itself, and
states of other scenes than Hyrule Field and Link's house (run `tools/state-census` on a state
of each; a holder nobody listed crashes on load into a different build).

**Start here (2026-10-13, end of the session):** everything is merged into `release/v0.1.2`
and pushed. Next, in this order: **P2.6** (A/B/X/Y as a diamond in the in-game HUD, spec
below), then issue #4 (the relaunch after `SITE INSTALL` sometimes fails: the loader runs out of
memory, see its last comment), then the two checks left on issue #11 (a border scroll up and
one down in Full View, and one frame at the scroll's start where the HUD vanishes).

**On the owner's consoles now (2026-10-13):**
- Old 3DS/2DS, **`192.168.1.132`** (not in `.3ds_ftp_ip`: pass `--ip`): a debug build of
  `release/v0.1.2` with `fix/light-rays-full-view`, `feat/tabbar-position`, `feat/hud-xy-diamond`
  and `fix/chuchu-bad-child` applied together (2026-10-13), Full View (`widescreen=1`,
  `screen_aspect=wide`, `display_style=pixel-perfect`), theme PARCHMENT. The owner plays on
  it (Minish Woods, sword and shield only) and keeps save states at spots that reproduce bugs:
  never overwrite or delete one (protect their place with a *new* state: `savestate`, then
  `loadstate 1` and `delstate 1` after the install).
- New 3DS, `192.168.1.133` (the one `.3ds_ftp_ip` holds): not touched since 2026-10-12.
- The owner's Luma dumps are in `sdmc:/luma/dumps/arm11/` (reachable over the game's FTP).

**Older notes (2026-10-12), still open:** check `perf/full-view-old3ds`-era Wide (266x160,
interiors and map edges) and other areas (Deepwood, Mt. Crenel) on the 2DS; per-frame cost left
there: the rings 3 ms (character-data digest 1.3 ms, cell compare 1.5 ms), OBJ/other quads
~0.5 ms; VRAM write tracking in the PPU model would drop the digest.

**Owner's request, 2026-10-12 (P3.4): everything through the GPU.** The message shift (dialogues
and the location-name banner, `virtuappu_mode1_ws_msg_*`), the HUD right-anchor and Wide BGs
with mosaic still go through the per-pixel path (the banner runs at ~52 fps, G 6.2 ms, on the
New 3DS). Teach the GPU renderer `ws_msg_shift` first: it is what dialogues and every first
visit to an area hit.

**Smaller open items:**
- The busy window of a save state job (SAVING / LOADING / DELETING STATE) is bottom screen only
  and was never seen on the console (the link's SHOT runs on the game thread, which the job
  stops); the owner may want a notice on the top screen as well.
- Re-save the older states (see the decisions log of 2026-10-12): repaired on load, but they
  may keep other stale words.

**Open investigation (2026-10-12):** the enemy's dissolve in the castle garden is wrong in Wide
(BG3 under HDMA, "native bounds"): issue #5 and
[`docs/investigations/bg3-dissolve-wide.md`](investigations/bg3-dissolve-wide.md) (reproduction,
findings, images, branches). Read it before touching BG3 / `mode1_bg3_native_bounds_active`.
Branches not in the release: `fix/affine-obj-uv-inset` (sword, checked), `fix/bg3-native-bounds-gpu`
(partial), `wip/bg3-vertical-experiment` (experiment, not a fix). The 2DS at 192.168.1.139 runs the
experiment build, FRAMES = SIN SALTOS, VISTA = ANCHO.

**Priority** (owner's order, 2026-10-11; 2026-10-13: P2.6 first, then issue #4): P0.1 → P1 debug tools and P2 bottom UI (so
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
  Transport (2026-10-11): SITE commands on the in-game FTP server instead of a second TCP
  server: one port, one switch, already restored at launch. Unattended scenarios after an
  install (owner): `tmc-link.py wait`, then a `run` script (load state, buttons, shots).
  First cut done: buttons, taps, screenshots, load state, dump, switches, state line,
  memory by symbol. Left: pause/step, warp, live tunables, setting any OPTIONS value.
  *Done when:* Claude, from a shell here, takes a screenshot, reads Link's position, changes a
  switch and injects a button press on the owner's console, and the owner sees it happen.
- [ ] **P1.8** Save states.
  *Spec:* numbered states with thumbnail and place (like `../sm-3ds` STATES),
  available in every build. Done as a raw copy of the game's data with the game's layout
  pinned by `platform/3ds/3dsx.ld` (docs/save-states.md), not on `port_dump_state.c`
  (upstream's "load state" only reloads the save file).
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
- [x] **P2.4** The bottom screen's own menu style.
  *Spec:* the same boxes in every tab (rounded corners of the game windows, 1x, theme colours),
  toasts and windows in the message window's frame, the world map without the stone frame
  (Link's head as its marker), the quest lists' slab and wells through the same boxes; no game
  pixels scaled (decisions log, 2026-10-13).
  *Done when:* the owner approves it on the console (2026-10-13, merged).
- [x] **P2.5** X and Y item buttons.
  *Spec:* X and Y (also ZL/ZR) fire items like B through the port's soft slots
  (`port/port_softslots.c`, wired into the 3DS input path in `port/port_bios.c`), assigned
  from the bottom screen: tap A/B/X/Y in the sidebar's diamond (it stays selected), then an item
  in ITEMS; a second tap on X/Y's item clears it. Kept for every save file (`tmc.softslots` in
  the data folder); an item the save does not own does nothing.
  *Done when:* the owner fires items with X and Y on the console (2026-10-13, sword and shield).
- [ ] **P2.6** A/B/X/Y in the in-game HUD.
  *Spec:* the top screen's HUD shows four item buttons in the console's diamond (X on top, Y
  left, A right, B below; owner's choice, 2026-10-13) in the style of the game's A/B buttons:
  the button bubbles of sprite 505 (504 on EU), frames 0/1, with X/Y letters drawn into a copy
  of the bubble, and each button's item icon as the HUD draws A/B's (gfx loaded per equipped
  item, `src/ui.c` ButtonUIElement / the item elements). Where the HUD is anchored right in
  Wide / Full View the diamond follows it. Done in the engine (`DrawHudXY`, `port/port_hud_xy.h`):
  see the decisions log, 2026-10-13.
  *Done when:* the owner sees the four buttons with their items in normal, Wide and Full View.

- [ ] **P2.7** Tab bar on top or below.
  *Spec:* where the bottom screen's tab bar goes is its own option, OPTIONS -> TABS (TOP / BOTTOM),
  next to THEME (owner, 2026-10-13); every theme is only a palette (FOREST used to move the bar).
  Saved as `ui_tabs_bottom`; a config without it and with FOREST keeps the bar below.
  *Done when:* the owner switches it on the console in each theme.

## Phase 3: performance (60 fps, New 3DS and Old 3DS, no 3D)

Measured with P1 tools, one change at a time, on hardware; numbers in the table
below and the decisions log.

The goal is 60 with Wide + Pixel Perfect on **both** models, the Old 3DS/2DS included.
Reference renderers (owner's other ports, both GPU-only and at 60 on an Old 3DS):
`../mzm/platform/3ds/source/port_gpu_renderer.c` (a GBA PPU on the PICA, already draws a
400x240 wide view; its lessons in `../mzm/docs/3ds-renderer-perf-plan.md`) and
`../sm-3ds/source/gpu_ppu.c` (a **SNES** PPU, so its ideas carry over but not its code;
`../sm-3ds/docs/gpu-ppu-design.md`: whole tilemaps decoded
into BG textures with dirty tracking, one quad per scroll run, a sprite atlas; ~3 ms of
build on a 2DS at 268 MHz). Ours (upstream's, per-tile quads) spends 10.3 ms building on a
New 3DS at 804 MHz (see the baseline). `docs/renderer-gpu-gap.md` compares what the game
needs with mzm's renderer: mzm's lacks per-scanline windows and scrolls, affine BG2 and
mosaic, which ours has, so ours stays and takes their build techniques (the order is there).

- [ ] **P3.1** Find where the frame goes on each model (from P0.2): logic vs PPU
  model vs GPU build/submit vs present vs bottom screen vs audio, and how often the
  software fallback runs.
- [ ] **P3.2** New 3DS at a locked 60 in every P0.2 spot, Wide + Pixel Perfect.
- [ ] **P3.4** Everything through the GPU.
  *Spec:* owner's rule (2026-10-11): every frame is drawn by the GPU renderer on both models;
  the software PPU stays only as a comparison (DEBUG -> SWITCHES -> RENDERER, GPU CHECK). Done
  so far: the renderer runs on the New 3DS too (upstream kept it to the Old 3DS). Left: the
  frames that still fall back to software, each to be taught to the GPU renderer: the New 3DS
  Full View layout (400x240 outdoors; indoors run the 1x Wide path), command-budget overflows,
  build failures (`PpuGpu3DSBuildReason`). The perf recorder's `gpu` column and the SD log's
  `fallback` count show how many are left.
  *Done when:* a long session on each model records `gpu` = 1 on every gameplay frame.
- [ ] **P3.3** Old 3DS/2DS at a locked 60 with Wide + Pixel Perfect (Full View) in every
  P0.2 spot (owner, 2026-10-11: the target is the same as the New 3DS; no 30 fps fallback).

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

First perf recordings (2026-10-11, build `0.1.1-dev.13.9+1774bfb`, New 3DS, Hyrule Field
03/01, FRAMES = NO SKIP, walking ~25 s each; `tools/perf-csv/analyze.py`):

| Setting | fps | Drawn by | Per frame (mean) |
|---|---|---|---|
| Wide + Pixel Perfect (Full View 400x240) | 33.8 | software PPU, every frame | sw render 28.8 ms; every frame takes 2 vblanks |
| Wide + Bilinear | 57.5 | GPU, every frame | gpu build 10.3 ms + gpu begin (waiting for the previous GPU frame) 6.3 ms = 16.9 ms, just over the 16.7 ms budget; vblank wait 0 |

After `perf/wide-bg-tile-runs` (same spot, Wide + Bilinear): 59.8 fps, build 5.1 ms (`bg` 4.9;
it was 10.4: Wide BGs cannot use the map-space path and were sampled pixel by pixel), vblank
wait 10.7 ms. After `perf/full-view-gpu`: Wide + Pixel Perfect (Full View) on the GPU at
59.8 fps, build 6.3 ms, vblank wait 9.6 ms; Wide + Bilinear build 4.3 ms.

Engine logic is 0.2-0.3 ms in both: the whole frame is drawing. The bottom screen paints on
its own thread and does not show in the top's interval.

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
- **2026-10-11** Performance target (owner): 60 fps with Wide + Pixel Perfect on the Old
  3DS/2DS as well, not only the New 3DS; P3.3 no longer allows a stable 30. The first
  recordings (baseline) put our GPU renderer's build at 10.3 ms per frame on a New 3DS,
  against ~3 ms on a 2DS for sm-3ds's, so mzm's and sm-3ds's renderers are the starting
  point for P3 rather than tuning upstream's.
- **2026-10-12** Save states are build independent (P1.8b, format 2), as mzm's: the game's globals by
  name and pointers as relocations, from a symbol table patched into the ELF after the link
  (`tools/gen_state_symbols.py`). Owner's rule: a state must load whatever the build. The raw
  copy of 2026-10-11 stays as format 1. Tested by saving, adding a global and a function to the
  game side (text +0x28, .bss +0x1230), installing and loading: position exact, game goes on.
  A first attempt crashed in `UpdateBgAnimations`: `gBgAnimations` points into the middle of
  constants, which only `kConstInner` covers. Only Hyrule Field was checked with the census.
- **2026-10-11** Save states as a raw copy of the game's `.data`/`.bss` with the game's code,
  constants and data laid out first at pinned addresses (`platform/3ds/3dsx.ld`), instead of
  mzm's globals by name with relocated pointers: far less code, and enough for the loop the
  owner asked for (Claude installing port builds and reloading a state on each). A change to
  the game's sources refuses older states; mzm's format is the way out if that ever matters.
  The fingerprint covers the layout, not the game's code bytes, which change with every port
  change (they hold the addresses of port functions). The tab bar's theme button is gone
  (OPTIONS -> THEME does it; owner).
- **2026-10-11** The bottom screen shows all of its UI outside gameplay (owner: everything
  must be there to interact with, locked where it needs a game). Upstream's title card and
  its OPTIONS-only sheet with a BACK button are gone; it opens on OPTIONS, and MAP, ITEMS
  and QUEST show a notice until a game is loaded.
- **2026-10-11** The updater shows the `<!-- tmc-notes -->` block (our release
  notes) and falls back to upstream's `## Changelog` section.
- **2026-10-11** Interiors are never drawn at 2x (owner: the pixel size must be the same
  everywhere in the game). `Port3DSFullViewPolicy_Decide` no longer returns
  `INTERIOR_2X`; indoors fall back to the 1x Wide path (reason `interior-1x`). The enum, the
  200x120 renderer and its presenter geometry stay in the code, unused.
- **2026-10-11** Relaunch after `SITE INSTALL` requests the jump with the GPU alive and closes
  through the normal exit path (drain hook, teardown on a thread with a timeout), as `../sm-3ds`.
  Tearing citro3d down *before* `APT_DoApplicationJump` (`fe7780a`) hung one relaunch in two on
  the console (logs ended at "relaunch: gpu down, jumping"); after the change, six installs in a
  row from the new build went through. The first install onto the new code still goes through
  the old exit path and can hang once.
- **2026-10-12** Dialogues keep Full View (owner: wide must not be lost in dialogues). A message
  no longer counts as a UI overlay in `Port_WidescreenFullViewUiOverlayActive`: the box is
  centred by the same `ws_msg_shift` the E2 Wide path uses, which already follows the view
  width. Room-name banners and native windows still fall back to E2. Checked on the console
  with an NPC dialogue in Hyrule Field (box at the top, 400x240 kept, no flip at its end). A
  box that the game puts at the bottom of the 160-row canvas has not been seen in Full View:
  it would sit 80 rows above the bottom of the 240-row view.
- **2026-10-12** Leaving a house showed the room at 240/266 for the whole fade-in and popped to
  400 on a lit screen: the fade counted as a room transition. The fade is out of the transition
  test now (`Port_WidescreenRoomTransitionActive`: the out phase, reload flags and the border
  scroll are what mix two map generations), and Full View needs 14 stable ticks instead of 24.
  Log of leaving Link's house: 240 (fade-out) -> 240 and 266 for one frame each at the start of
  the fade-in -> 400 fifteen frames later. Checked by log and by screenshot after; the frames in
  between were not seen.
- **2026-10-12** Full View now stays through the fade-out of a door and the room change shows
  as one early switch under the fade-in (owner: the wide disappeared before being inside a
  house). `Port_WidescreenRoomTransitionActive` counts only what replaces the room's data:
  GAMEMAIN_INITROOM / CHANGEROOM (CHANGEAREA once its fade ended), the reload flags and the border
  scroll; `transitioningOut` and the fades are out. Entry needs 4 stable ticks. Log, three
  round trips Hyrule Field <-> Link's house: entering keeps 400 until the new room's first frame
  (240 interior); leaving goes 240 -> 266 (+4 frames) -> 400 (+6 frames) from the first frame of
  the field, under the start of the fade-in.
- **2026-10-12** Leaving a house still showed 240 -> 266 -> 400 under the fade-in (owner saw a
  "strange frame"). The top screen now holds its last frame (`Port_Widescreen_3DSHoldTop`) while
  a fade is on, the room changed within 16 ticks, the area is an exterior and Full View is not
  published yet: the held frame is the fully faded one, and the first frame shown is already
  400x240. Never during a border scroll (no fade). The view log marks held frames with HOLD. Log,
  two round trips: 240 / 266 frames at the black, 4 held frames at fade start, then 400.
- **2026-10-12** The "1+X+1" frame of leaving a house was the field's first frames at 266 on a
  white fade: a white box wider than the interior's, then the 400 view. Found with the new frame
  recorder (`SITE REC`, docs/debug-tools.md). The hold starts at the room change of a door (a
  fade within the last 90 ticks, not a border scroll), not when the fade-in starts, so the white
  box keeps the interior's width until the 400x240 frame is ready: one change instead of two.
- **2026-10-12** The bars beside a narrow view take the fade's colour (`Port_FadedBlackRGB` in
  `src/fade.c`, used for the presenter's clear): a door's white fade now covers the whole
  400x240, so leaving a house is white all over and the field comes out of it at full width.
  Checked with the frame recorder. Black fades are unchanged.
- **2026-10-12** Boot screen: the bottom screen shows a gold triforce turning (own thread, drawn
  straight into the gfx framebuffer, stopped before `Port_PPU_Init` where citro3d takes the
  screen) instead of the console, and stdout/stderr are silenced from the start
  (`Platform3DS_SilenceBootConsole`): the ROM loader's `fprintf(stderr, ...)` was painted over
  it. The animation swaps only the bottom screen's buffers (`gfxScreenSwapBuffers(GFX_BOTTOM)`);
  `gfxSwapBuffers` swapped both and made the top splash flicker. The splash
  (`romfs/splash.rgb565`) was a cropped logo; it is regenerated from `assets/splash.png` with a
  margin, written to both top buffers, and the fixed 1.2 s sleep is gone.
- **2026-10-12** The SHOW FPS box is four lines like mzm's (game rate, `L` logic ms, `G` GPU ms,
  `S` shown rate). Its rectangles are citro2d objects: with `C2D_Init(128)` the box ran the
  frame out of objects and citro2d silently dropped later draws, among them the bottom screen's
  image (a black bottom on the frames with wide digits). `C2D_Init(512)`. Lesson: anything that
  draws text rectangle by rectangle must be counted against that limit.
- **2026-10-12** Save states, pointers the rules did not cover: `gMapBottom`/`gMapTop` are 49 KB
  and were never scanned, but their first word points into `gScreen`; and pointers into the
  middle of `gScreen` were relocated only from known holders. After a rebuild that moved
  `gScreen` the view stayed at 240x160 (`latch-mismatch`) and the oldest state crashed
  `LoadBgAnimationGfx` (stale `gBgAnimations` frame pointers). Rules added (`kHeadOnly`,
  `kInnerTargets`), old states repaired on load (`RepairScreenPointers`, entries of
  `gBgAnimations` that did not come back as a relocation end the list) and the widescreen latch
  is reset after a load (`Port_Widescreen_ResetAfterStateLoad`). Found with the SD log's view
  lines, `peek` over the link and the Luma crash dump.
- **2026-10-12** A tap on a tab was lost while another panel's repaint was in flight: the touch
  gate drops taps until the last repaint is visible, and the DEBUG tab repaints almost
  continuously (3 of 12 alternating taps lost, measured). The tab bar is the same in every
  panel, so a tab tap skips the generation gate and keeps the mode check
  (`Port_SecondScreen_TapIsTab`). Re-tapping the active tab still returns to the map, by design.
- **2026-10-12** Busy window: a save state job runs between frames and stops the game (save
  ~2 s, load ~0.3 s), so nothing could be drawn while it ran. `SaveState_Service` waits eight
  frames after a job is asked (not for the scan) and the bottom screen paints SAVING / LOADING /
  DELETING STATE over everything, taking every tap (`Compact_PaintBusy`). Only on the bottom
  screen; the top one keeps its last frame. Not seen on the console yet: the link's SHOT runs on
  the game thread, so it cannot capture a frame during the job.
- **2026-10-12** The HUD buttons jumped to the middle of the top edge when they hid (approaching
  Zelda, Wide / Full View): `HudButtonTargetX` dropped the Wide anchor while `hideFlags` was
  set, and `Port_UI_PrepareGameplayGeometry` moved them at once. The anchor holds now; they slide
  up from their corner. Seen and checked with the frame recorder, which now also starts on a
  change of the hide flags.
- **2026-10-12** The location-name banner (entering Hyrule Town) kept the view at E2 266x160 for
  its ~4 s, then Full View took over with a camera snap. It was counted as a UI overlay in
  `Port_WidescreenFullViewUiOverlayActive`; it is a BG0 band that `ws_msg_shift` already centres
  at any width, like a dialogue, so only native windows count now. Full View is reached 6
  frames after the room loads, under the hold, and the scroll through the gate is continuous.
  Not CPU/video: the same GPU renderer, but the shifted band goes through the per-pixel path,
  so the frames with the banner run at ~52 fps on the New 3DS (G 6.2 ms).
- **2026-10-12** Full View on the Old 3DS/2DS (owner: wide + pixel perfect at 60). The New 3DS
  gate of `Port_Config_3DSFullViewComboEnabled` is gone. First measurement on the 2DS, Hyrule Town:
  400x240 at 31 fps, build 22 ms, all of it `bg`: 7000 quads a frame at ~2.5 us each (the ARM11 has
  no L2; every quad touches the tile memo, the cache entry and 64 bytes of vertices). Skipping
  fully transparent BG tiles (-4 ms) and a bigger tile memo (nothing) were not enough, so the
  per-tile geometry went: **view rings**, one 256x512 texture per BG and variant (VRAM map /
  shadow map) holding the tile map transposed, 64 columns x 32 rows of 8x8 cells indexed by
  absolute map column, REPEAT-wrapped. A frame compares each visible cell's entry and palette
  generation against what it holds (one word), decodes the ones that differ straight from VRAM
  with the bank LUT, and draws one quad a band whose UVs carry the scroll; scrolling replaces a
  column of cells. Character data changes are found by hashing the 256-byte chunks the ring has
  used. Result: build 22 -> 3 ms, 59.8 fps walking. Layers that keep the quads: BG0 while a message is
  shifted, mosaic, 64-tile maps, affine, bands that disagree on the scroll column. GPU CHECK on the
  2DS: 0 structural pixels; ~10000 one-step pixels in the cloud shadows (an alpha-blended BG over
  a ringed BG, always +1; with rings off it is 0 here, the New 3DS baseline had thousands too):
  not chased. `gPpuGpu3DSRingDisable` (bit per BG, `tmc-link.py poke`) switches rings off to compare.
  The 2DS crash dump the owner brought (a `pc` in the linear heap, process "Minish C") matched no
  frame of the new build: the log showed the build running; it is the known relaunch hang of
  `--install` (issue 2).
- **2026-10-12** Bottom screen, one style (owner: every window in the new style, no embedded
  game menus; item sprites stay). Done, uncommitted on `perf/full-view-old3ds`: UPDATE page and
  its changelog window in the compact style (the top screen keeps the game; the game no longer
  pauses while it is open), BACKDROP option gone (the theme owns it), STRETCH view gone (old
  files load as WIDE), COLOURS works on the GPU renderer, ITEMS / QUEST / sidebar / dungeon
  floors / map chip redrawn with the theme (`bottom_compact_3ds.inc`), `tools/ui-preview` stubs
  brought up to date. Still to do: check them on the console with a save that has items, the
  world map's stone frame (crop it inside the frame instead of the jagged edge), the two quest
  sub-lists (kinstones, techniques) still use the game's art, the region zoom, the load-state and
  notice modals are reachable only from dead pages.
- **2026-10-13** Leaving the castle garden (an area change, white fade) showed ~5 frames at
  266x160 before Full View, and the fade-in from white was lost: `GameMain_ChangeRoom` lasts the
  whole fade-in of an area change, and counting all of it as a room transition kept Full View
  off until the fade had ended, under the top hold. CHANGEROOM counts only for its first two
  ticks now (the room is loaded by then), and the hold may run 24 ticks past its 16 while the
  policy wants Full View and only the latch is missing. The first half of the fade-in is still
  under the hold (white).
- **2026-10-13** The network service (SOC and AC, `net_3ds.c`) is never shut down while the game
  runs: `socExit` hung the main thread when the FTP server stopped with a client connected
  (issue #9). It costs the 1 MB SOC buffer from the first FTP start or update check until the
  game exits; the kernel closes the sessions at process exit. The same change went into the
  sibling ports (mzm, sm-3ds, Legend of Doom), which share the code.
- **2026-10-13** The bottom screen is its own menu, the same in every tab (owner's choice over
  copying the pause menu's screens: those are laid out for 240x160 and the whole screen, the
  tabs here are not the game's, and a tab that imitates the original next to ones that do not
  reads as a change of style). The game supplies the content (item icons, fonts, the map, Link's
  head, sprites) and the shapes: the boxes copy the message windows' corner curve and the pause
  menu chips' keyline as geometry, drawn at 1x in the theme's colours, never the game's pixels
  scaled (upstream's attempt scaled 720p art and cropped it). The world map drops the stone
  frame; its crop is the map inside it (x 41..198, y 13..139 of the composite).
- **2026-10-13** X/Y items reuse the PC port's soft slots instead of new engine slots: the B
  dispatch and the held-item check already honour them (`Port_SoftSlots_GetEffectiveBItem`,
  `IsBHeld`), so held items keep their semantics without touching the save. The 3DS had never
  polled them: its VBlankIntrWait writes KEYINPUT from the platform directly and skips
  `Port_UpdateInput`, where PC polls them. Assignment selection is sticky (owner): the tapped
  button stays selected, A by default.
- **2026-10-13** The Minish Woods light rays (LightRayManager, gfx group 0x25) are a screen-fixed
  BG3 picture filling only the right half of its 256 px map, ending at the GBA screen's right edge.
  Full View repeats a 32-column BG from the view's left edge (mode1.c, `repeat_full_view_overlay`),
  which put a hard edge through the view; the GPU renderer did not even repeat it (only Hyrule
  Field's clouds had `bg3Repeat`) and cut it at x=240. Now a BG3 shadow anchors the one copy to the
  view's right edge (`Port_WidescreenLightRayShadow`), rows past the map's 32 read from gBG3Buffer;
  the GPU renderer repeats BG3 in Full View like the software one; and the "native bounds" rule for
  BG3 under HBlank DMA (issue #5) applies only while Vaati's appearing manager exists, since the
  other HBlank BG3 effects (rays in their other state, steam) are overlays of the whole screen.
- **2026-10-13** The tab bar's place is its own option (TABS), not part of a theme (owner): the
  themes are palettes, FOREST no longer moves the bar. SHOW FPS moved to a fifth OPTIONS row to make
  room beside THEME.
- **2026-10-13** X/Y in the top HUD (P2.6) are game sprites, not an overlay on the 3DS screen: the
  engine draws them after A/B with DrawDirect (the A bubble, sprite 505 frame 0, and the item's
  sprite-322 icon), so both renderers, the palette fades, the Wide/Full View anchor and the message
  slide treat them as the game's own HUD. OBJ VRAM has no 24 free tiles in gameplay (0x136-0x13F
  is all), so their OAM entries point at the top tiles 0x3E0-0x3F7 and `hud_xy_3ds.c` writes the
  tiles there only around the top screen's render (`Port_PPU_PresentFrame`) and puts the bytes
  back: the game never sees them. It checks that OAM still holds the entries drawn and that no
  other sprite uses those tiles that frame (a crowded scene: X/Y are hidden for the frame). The X
  and Y letters are drawn into a copy of the A bubble (4x5, the A's outline colour). Layout: A keeps
  its column, the diamond's middle row at y=42 under R and its label, 12 px steps (half the
  original A-B distance); X/Y are placed from A's live position.
- **2026-10-13** The R prompt follows the HUD: the button at the right, the game's label art
  (in the game's language) over it to its left, measured on a scratch strip first because
  `Port_SecondScreenTheme_DrawActionLabel` places by its centre.
