# Minish Cap 3DS (tinaut1986 fork): action plan

Living document. Read it at the start of every session; keep it current as things
change (what goes where: the table in `CLAUDE.md`).

- **Goal:** The Minish Cap on 3DS running **smoothly at 60 fps on both New 3DS and
  Old 3DS/2DS** (stereo 3D only on the layers the owner chooses, off by default), with a **wide mode that fills the 400x240
  top screen** both pixel perfect and scaled, by showing more of the world than the
  GBA did; debug tools and a bottom-screen UI on par with `../mzm` and `../sm-3ds`.
- **Starting point:** EstebanPdN's `zelda-tmc-3ds` v2.2-E1 (forked 2026-10-11). On
  the owner's consoles it is not smooth on either model, even with the 3D slider
  down. It already has a GPU tile renderer (`platform/3ds/source/port_ppu_gpu_3ds*.c`,
  on by default) with a software fallback, an Old 3DS adaptive frame pacer, NDSP
  audio offload, an updater, a quick memory dump (L+R+A) and an experimental New 3DS
  "Full View" (Wide + Pixel Perfect: 400x240 outdoors, 200x120 at 2x indoors; the 2x indoor zoom is gone, see the decisions log).

## Status (2026-10-18)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.3.3` (on origin; renamed from `release/v0.3.2` right after the
`v0.3.2` tag). Published: **`v0.3.2`** (2026-10-18, **stable**: first a beta, then promoted by re-running the
workflow on the tag after `release/v0.3.2` was merged `--no-ff` into `main`, `a42d406`; its release body's notes
were widened by hand to cover `v0.3.1` too, see the decisions log), **`v0.3.1`** (2026-10-17, **beta**), **`v0.3.0`** (2026-10-15, **stable**, merged into `main`, straight
to stable without a beta at the owner's request), `v0.2.0` (2026-10-14, stable), `v0.1.2`
(2026-10-13, beta), `v0.1.1` (2026-10-12, stable) and `v0.1.0` (2026-10-11, beta).

**Branches:** `feat/village-3d` is parked (local only, not pushed). Every other branch
merged into the release line was deleted, here and on origin (2026-10-17: `fix/title-live-view`,
`feat/gba-look`, `feat/hud-auto`, `feat/title-rays-3d`, `feat/minish-paths-centred`,
`feat/wide-ai-ask`, `feat/stereo-fileselect`, `feat/splash-3d`, `docs/plan-branches-2026-10-17`; the
remote `feat/retroachievements` is gone too and the local `test/crash-fix-and-map-chips`, `537b5fa`,
was deleted). Their history is in `git log` and the decisions log.

| Branch | State |
|---|---|
| `feat/wide-neighbour-rooms` | Cut from `release/v0.3.3`, not merged: P4.9 probe, the neighbouring room drawn beyond the edge (decisions log, 2026-10-18). Waits for the owner's look and the decision whether to go on. |
| `feat/village-3d` | Parked on purpose (owner, 2026-10-17): kept only to see whether it can be taken up again one day; **nothing of it goes into any release** because it causes visual problems (the Minish Village's flowers and leaf blocks in 3D: the houses go forward instead of back, the hanging fruits are not found). Local only, not pushed. Notes: `docs/stereo-3d.md`, "Minish Village". |

**What `v0.3.2` holds beyond `v0.3.1` (2026-10-17, all seen working on the New 3DS):** the garbage beside a narrow room whose BG3 has no blank
tile 0 (area 0x20 room 09), the village's gap cells taking the nearest zone's graphics, the top-frame HOLD
also for doors into interiors, a state saved inside a subtask keeping the room's entities (Link locked after
a map hint), the 3D depth of the map screens (hint and pause map), PLANE TINT / FORCE 3D / right-eye
screenshots, and the bottom map in COLOURS with its hints over the unrevealed regions, swapping with Link
every 32 game frames, and dark rooms kept in Full View (the light around Link drawn across 400x240, issue #32). Also stereo 3D drawing the screen-plane layers once (`docs/stereo-3d.md`) and the GPU frame built while the GPU draws the previous one (`docs/gpu-build-ahead.md`; checked by the owner on the New 3DS, 2026-10-18; not seen on the Old 3DS/2DS). v0.3.1 itself holds what the owner checked on 2026-10-17 (TOP HUD AUTO, GBA grid and
frame, WIDE + AI, the Minish Paths corridor centred, the 3D of P5.1, the heart in use, the village intro's
Minish walking in). Not seen on the Old 3DS: the stereo once-drawn base, the GPU build ahead, the grid on HIGH (frame rate), WIDE + AI.

**Save states across builds:** closed by the owner (2026-10-17). The history is in docs/save-states.md and issues #13, #14 and #31.

**Start here (2026-10-18, after the other sessions' merges and the owner's cleanup):** `release/v0.3.3` is pushed and
clean (nothing after the `v0.3.2` tag but this docs change), no branch or worktree is open but the parked `feat/village-3d`. Next, in this order:
1. **v0.3.2 stable** is published and the updater work is in `release/v0.3.3` (AUTO UPDATE with its boot prompt,
   the relaunch after an in-game update, the changelog of the betas on the way, see the decisions log), all
   seen working on the New 3DS. Not seen: a console that is not the New 3DS (the updater on the Old 3DS/2DS).
2. **Old 3DS/2DS check** of what only the New 3DS has seen: the grid on HIGH (frame rate), WIDE + AI,
   the file select and title without 3D, TOP HUD AUTO. The 2DS is `192.168.1.139` (its FTP answered
   `530 Login incorrect` on 2026-10-17: ask the owner what that console runs).
3. **Side strips for narrow map rooms (P4.6).** The owner is generating, with Antigravity, the 80 px on each
   side of the shrine yard (area 0x4A room 00; material in `~/Imágenes/outpaint-area-4A/`: template with black
   bands, mask, reference and `prompt.txt`, made with `tools/scene-extend/extract_map_room.py`). When the
   picture comes: check it (`check_extension.py`), then the game needs a way to draw a picture beside a map
   room (the Minish Paths corridor has one; map rooms have only black there). Same for every other small
   interior the owner wants to fill.
4. **#30** the right edge of the top screen breaks on room changes and while rolling (2DS only: the owner
   sees nothing on the New 3DS). Not a regression. The CPU/GPU race on the retained map slices (hypothesis 1) is
   out: the old order waited before every write and the build-ahead order keeps that rule by construction (decisions
   log, 2026-10-18). What is left is hypothesis 2 of the issue (the display transfer or swap landing mid-scanout,
   invisible to the scene recorder, which reads the framebuffer) and the CPU speed of the Old 3DS itself; look
   with `gpucheck` loops during scrolls on the 2DS and FRAME SYNC / VBLANK LOCK combinations.
   **#33** (opened by someone else, not the owner): "Wide+AI in Pixel Perfect" works in many areas but in some the
   view shrinks back with black borders on all sides, like the area before the Mt. Crenel dungeon. That is the
   Full View policy falling back (`Port_Widescreen_3DSFallbackReason`: the Mt. Crenel room 2 `unsupportedScene`,
   fixed canvases, rooms without content bounds); find out which areas and why before answering. Not looked at yet.
5. Smaller: the bottom screen's UI theme (hearts, buttons, sidebar chrome) is not corrected by COLOURS (only
   the game art is; `port_second_screen_color.h`); sm-3ds's DRAW ORDER tint view could join PLANE TINT; the
   light rays of the room 0x20/09 hang over its black border on the right (left as the game's rays);
   the shoe house's door and other scripted entrances may pop in Full View like the village intro's Minish did
   (look when the owner reports one); P3.4's old claim that dialogues go per pixel was not borne out (the C
   line of the numbers box read C0 through a dialogue), recheck before rebuilding anything; the generated
   side strips of the Minish Paths corridor (P4.6); the README's before/after GIF of the portal shrink (P1.6).
Parked (owner, 2026-10-17): **`feat/village-3d`**, none of it goes into a release; do not work on it unasked.
Closed by the owner (2026-10-17), do not re-propose: RetroAchievements (the achievements he looked at
work; he will report any that should fire and does not), the save states across builds, issue #5
(the dissolve works), the sibling updaters' hardening (done), #25, #4, #23, #26 and #20 (all closed on
GitHub).
Scene pictures (portal, title, file select, shrine yard): procedure in `docs/wide-scene-extension.md`; the owner's
material in `~/Imágenes/minish-portal-cutscene/`, `~/Imágenes/intro-titulo/`, `~/Imágenes/elige-archivo/` and
`~/Imágenes/outpaint-area-4A/` (never in the repository).
**Checking 3D without a 3D screen:** DEBUG -> SWITCHES -> FORCE 3D (slider fully up, not saved), then
`tmc-link.py shot` also writes `top-right.png`; PLANE TINT (LAYERS / DEPTH) paints each layer flat. `docs/debug-tools.md`.
To start a session's notes, run `/document` before closing it (`.claude/skills/document`).

**On the owner's consoles now (2026-10-17, newer than the lines below):**
- **New 3DS, `192.168.1.133`** (the owner, 2026-10-17: "es la 133 (new3ds)"; its address changes with the
  router: it answered on `.141` earlier the same day, and `.142` on 2026-10-14, so ping before assuming):
  a debug build of the code of `release/v0.3.2` at `aeb0c48` (`0.3.2-dev.*`; the perf sessions of 2026-10-18 may have
  installed newer builds since: read the version in `tmc3ds.log` or the options footer), WIDE + AI answered YES, VIEW wide,
  STYLE pixel-perfect, GBA GRID LOW, GBA FRAME off, TOP HUD AUTO, FRAMES "sin saltos", SHOW FPS on (debug
  builds show the fifth line `C<n>`), stereo on, COLOURS on. FORCE 3D and PLANE TINT are off (they reset at
  boot). State 1 is Claude's (the shrine yard, area 0x4A, saved before going out to take the pictures); the
  states made the same day before it (the Minish village, the map hint at the owner's place) are the owner's
  or Claude's from earlier installs: never overwrite or delete any, the owner's own were not touched. The owner
  tests while Claude drives the console and accepted several relaunches in one evening: say so before each
  install anyway. `.3ds_ftp_ip` may hold `.141`: pass `--ip`.
- `192.168.1.141` did not answer on 2026-10-17 afternoon (off or another address); the 2DS `.139` answers
  `530 Login incorrect`. Their entries below are from 2026-10-15 and may be stale.

**On the owner's consoles before (2026-10-15):**
- Old 3DS/2DS, **`192.168.1.139`** (the owner calls it "the 2DS"; `.3ds_ftp_ip` holds it): the
  debug build `0.3.0-dev.47.2+c363f1f` of `fix/states-build-independent` (what v0.3.0 shipped, minus
  the map chips and the two guards), debug, Full View (`widescreen=1`, `screen_aspect=wide`,
  `display_style=pixel-perfect`), theme PARCHMENT, **FRAMES = "sin saltos" (the owner set it)**,
  STEREO 3D ON (default; no effect on a 2DS), VBLANK LOCK OFF, FRAME SYNC OFF, COLOURS as the
  owner left it (`color_correction=1`), UPDATES channel + BETAS. **RetroAchievements is logged in
  as the test account `tinaut1986test`** (log in with the real one before playing for real); the
  data folder has no test ROM and `rom_md5` is the true `2af78edb...`; the owner's ROM is in the
  upstream app's folder. **`dev-ftp.txt` is there**. Save states: about 25, numbered by recency
  (`tmc-link.py states`); Claude saved some as "new state" before installs; **never overwrite or
  delete the owner's**; most were made by dev.42 or older, so loading them in dev.45.x can corrupt
  memory (#31). `tmc-link.py loadstate N` numbering did not always match the listing during the
  session: confirm the scene with `state` and a `shot` after loading. Loading a state switches
  the bottom screen to MAP: tap another tab after it, not before. The recorder (`rec`) must not
  be left armed: on the Old 3DS it drops the game to ~20 fps.
- **`192.168.1.141`** (pass `--ip`): **the 2DS** (owner, 2026-10-14; its log says Old 3DS, which a 2DS
  is to the system: no 3D slider). The owner's console for the title and file select work: a debug
  build of `fix/zone-tilesets` (`0.2.1-dev.0+42f5761`, the village fix only) with **STEREO 3D =
  TEST**, Full View, SHOW FPS on, theme default. Its newest states are Claude's, the owner's places
  in the Minish village before installs (2026-10-14).
- **`192.168.1.142`** (pass `--ip`): **the New 3DS** (owner, 2026-10-14), a debug build of the code
  `release/v0.2.1` has since 7a65202 (`0.2.1-dev.0.10+a0bc40c`, from a local test branch since
  deleted), STEREO 3D used in ON and TEST, FTP server on.
  The owner's game is in Hyrule Castle (area 0x80); its state 1 is Claude's, the owner's place before
  the last install.
  `debug/` holds checks up to `tmc-check-0198` and perf CSVs up to `tmc-perf-0039`.
- The owner's Luma dumps are in `sdmc:/luma/dumps/arm11/` (reachable over the game's FTP); 16 is
  the newest on the 2DS (2026-10-15, #31). How to read one: `docs/debug-tools.md`, "Reading a Luma
  crash dump".

**Older notes (2026-10-12), still open:** check `perf/full-view-old3ds`-era Wide (266x160,
interiors and map edges) and other areas (Deepwood, Mt. Crenel) on the 2DS; per-frame cost left
there: the rings 3 ms (character-data digest 1.3 ms, cell compare 1.5 ms), OBJ/other quads
~0.5 ms; VRAM write tracking in the PPU model would drop the digest.

**Owner's request, 2026-10-12 (P3.4): everything through the GPU.** (2026-10-12 notes; the GPU model has had the message shift since, check the C line before trusting them.) The message shift (dialogues
and the location-name banner, `virtuappu_mode1_ws_msg_*`), the HUD right-anchor and Wide BGs
with mosaic still go through the per-pixel path (the banner runs at ~52 fps, G 6.2 ms, on the
New 3DS). Teach the GPU renderer `ws_msg_shift` first: it is what dialogues and every first
visit to an area hit.

**Smaller open items:**
- The busy window of a save state job (SAVING / LOADING / DELETING STATE) is bottom screen only
  (since #22 the job waits until it is shown); the owner may want a notice on the top screen too.
- Re-save the older states (see the decisions log of 2026-10-12): repaired on load, but they
  may keep other stale words.

**Open investigation (2026-10-12):** the enemy's dissolve in the castle garden is wrong in Wide
(BG3 under HDMA, "native bounds"): issue #5 and
[`docs/investigations/bg3-dissolve-wide.md`](investigations/bg3-dissolve-wide.md) (reproduction,
findings, images, branches). Read it before touching BG3 / `mode1_bg3_native_bounds_active`.
Not in the release: `fix/bg3-native-bounds-gpu` (partial, on origin); `wip/bg3-vertical-experiment`
(an experiment) is in neither this checkout nor origin any more.

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
  *Spec:* RAM ring of the shown top screen, one frame in N, written on stop as
  `tmc-scene-NNNN.bin` (400x240 RGB565, not sm-3ds's RLE); `tools/scene-rec/decode.py` → PNG/GIF.
  Driven over the link (`tmc-link.py scenerec`); no DEBUG cell yet.
  Done in `feat/scene-recorder` (merged 2026-10-13): 89 frames (3 s at 30 fps) on the 2DS.
  *Done when:* a few seconds of play decode to a video on the PC, and the README's before/after
  GIF of the portal (owner's request, 2026-10-13) is recorded on the New 3DS (more memory).
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
- [x] **P2.6** A/B/X/Y in the in-game HUD.
  *Spec:* the top screen's HUD shows four item buttons in the console's diamond (X on top, Y
  left, A right, B below; owner's choice, 2026-10-13) in the style of the game's A/B buttons:
  the button bubbles of sprite 505 (504 on EU), frames 0/1, with X/Y letters drawn into a copy
  of the bubble, and each button's item icon as the HUD draws A/B's (gfx loaded per equipped
  item, `src/ui.c` ButtonUIElement / the item elements). Where the HUD is anchored right in
  Wide / Full View the diamond follows it. Done in the engine (`DrawHudXY`, `port/port_hud_xy.h`):
  see the decisions log, 2026-10-13.
  *Done when:* the owner sees the four buttons with their items in normal, Wide and Full View
  (2026-10-13).

- [x] **P2.7** Tab bar on top or below.
  *Spec:* where the bottom screen's tab bar goes is its own option, OPTIONS -> TABS (TOP / BOTTOM),
  next to THEME (owner, 2026-10-13); every theme is only a palette (FOREST used to move the bar).
  Saved as `ui_tabs_bottom`; a config without it and with FOREST keeps the bar below.
  *Done when:* the owner switches it on the console in each theme (2026-10-13).

- [ ] **P2.8** RetroAchievements tab (owner's request, 2026-10-13).
  *Spec:* as in `../mzm` and `../sm-3ds`: rcheevos (softcore), login with the system keyboard,
  on/off, notice on the top or bottom screen with its sound and a sample, the set as cards with
  badges, sortable, a detail per achievement; progress saved with each save state
  (`docs/retroachievements.md`). The game's memory at its GBA addresses through
  `tools/gen_ra_map.py`. A first attempt with another tool had left only a stub.
  *Done when:* the owner logs in on the console, the list and badges load, and an achievement
  unlocks in play with its notice and sound (and is on the RA site).
  *Checked 2026-10-15 (owner, test account, USA hash given to the EU ROM):* login, the set (66
  achievements) and an unlock with its notice work; not yet: the sound, the RA site, the EU ROM
  under its own hash (RA lists only the USA one). Left open: issue #28 ("Offense").

- [x] **P2.9** TOP HUD: AUTO (owner's request, 2026-10-17; seen working on the New 3DS the same day).
  *Spec:* OPTIONS -> TOP HUD cycles SHOW / HIDE / AUTO (`hide_top_hud=0|1|2`; a config of an older
  build holds 0 or 1 and keeps its meaning). AUTO hides the top screen's HUD (hearts, charge bar,
  rupees, keys, the item buttons) exactly while the bottom screen shows the sidebar that has the same
  things (the MAP, ITEMS and QUEST tabs, with a game running), and shows it on the other tabs
  (OPTIONS, STATES, RETRO, DEBUG). `Port_Config_GetHideTopHud` answers for the moment, so the engine's
  gate (`src/ui.c`) and the snapshot's `topHudHidden` need no change; the paint thread publishes the
  tab (`Port_SecondScreen_TopHudCovered`).
  *Done when:* the owner sees the HUD go and come back as he changes tabs, and that nothing is left
  half drawn (the hearts repaint) on the way back.

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
  Since 2026-10-17 the numbers box (SHOW FPS, debug builds) has a fifth line, `C<n>`: frames the
  software renderer drew in the last second (green 0, red otherwise), so dialogues and area entries
  can be watched live. The GPU model already has the message shift (`ringMsgPiece`), so the old
  claim that dialogues go per pixel needs this measurement before anything is rebuilt.
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
- [ ] **P4.3** Cutscenes in Full View: the Minish portal (owner's request, 2026-10-13).
  *Spec:* the portal's falling cutscene (`SUBTASK_PORTALCUTSCENE`) at 400x240: its 256x320
  picture (BG1) centred, the camera following Link over 240 lines, BG1 repeated across the view
  and the wrapped columns covered by the presenter's side strips from `portal-cutscene-strips.bin`
  (shipped in the romfs, or the player's own; `tools/scene-extend/to_console.py`), faded with the
  game's fades.
  Port side only through the published view key (no static added, issue #19).
  *Done when:* the owner sees the cutscene at 400x240 on the 2DS, black sides and with a strips
  file made from a generated extension of the picture.

- [x] **P4.4** The title screen in Full View (owner's request, 2026-10-13).
  *Spec:* the intro/title at 400x240: only the forest background extended (a picture the owner
  generates from a dump, read from the SD card as in P4.3), the logo, sword and the rest centred as
  they are; the light rays between the trees stretched or repeated to the edges if they can be.
  The forest is 240x160, so it needs all four sides (80 left and right, 40 above and below). The
  picture's procedure (dump, template, prompt, check): `docs/wide-scene-extension.md`; the owner's
  material and the Antigravity prompt: `~/Imágenes/intro-titulo/` (never in the repository).
  *Done when:* the owner sees the title at 400x240 on the console.
---

- [x] **P4.5** The file select screen in Full View (owner's request, 2026-10-14).
  *Spec:* the cave around the screen from the owner's generated extension, shimmering with the
  original's palette cycling (`docs/wide-scene-extension.md`, "A generated picture over a
  palette-cycled background"), shipped in the romfs: **done** (the presenter draws it around the
  240x160 frame, `CaveReady` in `platform_gpu_3ds.c`). **Still to do:** what slides must leave by
  the 400x240 screen's edge, not vanish at the GBA screen's: the file rows and the language row
  (sprites to x = -72 when another is chosen), the header (type 18, to x = -96), the R "options"
  button and its label (`HideButtonR`, x = 0x140; slides 4 px a frame). All are sprites (OBJ):
  `src/object/fileScreenObjects.c` (`sub_0808EF6C` moves them, `gUnk_08121D54` starts them),
  `src/fileselect.c` (`HideButtonR` / `ShowButtonR`), `src/ui.c` (`ButtonUIElement_Action1`).
  Agreed design (2026-10-14), GPU renderer only: the frame at 400x240 like the title; BG0-BG2 (the
  windows and text, 256-wide maps that would repeat) drawn only in the centred 240x160; the cave
  as BG3 under the sprites (the picture as palette indices, from the BG palettes the frame uses,
  replacing the presenter's); sprites placed +80, +40, and past the GBA screen's edge carried on
  faster so a sprite the game parks at x = -72 really leaves the 400 view (the game's logic
  untouched). The software renderer does not have to follow (owner: everything through the GPU).
  *Done when:* the owner sees the menus slide out by the wide screen's edge on the console.

- [ ] **P4.6** Full View in every room, interiors and small rooms included (owner's request, 2026-10-14). *2026-10-17: the Minish Paths corridor (area 0x11) is done and seen on the New 3DS, merged: centred at view columns 10-39 with 80 black pixels each side, waiting for the owner's generated side strips (`docs/wide-scene-extension.md`). Narrow map rooms (houses, shrines) are black at the sides; the owner is generating the strips for the shrine yard, area 0x4A room 00 (Start here, 3).*
  *Spec:* with Full View the top screen is always 400x240. A room larger than the view scrolls; one
  smaller in either axis is centred in it with the rest black (the camera rests outside the room:
  `Port_WidescreenCameraRestXForMode`, `Scroll1`), and the HUD stays at the screen's corners. The
  policy only waits for the painted-content scan. **Done in code (needs the owner's check):** castle,
  houses. **Still to do:** dungeons with BG3 parallax or windows, cutscenes that move the camera
  by hand. **The corridor before the Minish village (Minish Paths, area 0x11, rooms 00-04, all
  240x800) is in progress on `feat/minish-paths-centred`:** `Port_WidescreenParallaxShadows` now
  also publishes shadows in Full View and places the room's 240 columns at view columns 10-39,
  masking the rest (black borders, 80 px each side; the owner will generate side strips with a
  generative AI, 80x256 per layer and side, tileable vertically: BG1 leaves, half speed; BG3
  shadows, quarter speed; BG2 is the ground map; `docs/wide-scene-extension.md` is the flow).
  **Bug found and fixed (owner confirmed on the console, 2026-10-15):** while scrolling, the bottom
  ~48 px (tile rows 24-29) of the BG1 leaf layer lost its leaves and the ground's bushes showed.
  Cause: the vertical corridors point the layer at a 32-row window of a taller map and scroll it
  by `yOffset & 0x3f`; the GBA's 160 lines never leave the window, the 240-line view wraps to the
  window's first rows. `Port_WidescreenParallaxShadows` now reads the slots above the first
  displayed row from the map rows that follow the window. Check the other four rooms too.
  *Done when:* the owner sees small and large interiors, the corridor and a dungeon at 400x240 on
  the console.

- [ ] **P4.7** GBA grid and GBA frame on the top screen (owner's request, 2026-10-17; from `../mzm`).
  *Spec:* two options in OPTIONS (the second row of cells): **GBA GRID** (OFF / LOW / MED / HIGH,
  `gba_grid`) darkens one screen row in every two of the game's own pixels, HIGH also the columns,
  lined up with those pixels at any scale, and **across the whole 400x240 in WIDE, border included**
  (the logos' white backdrop gets the lines too; the other views only the picture); **GBA FRAME**
  (`gba_frame`) draws the console's body in the border around a picture that leaves one: the 240x160 of
  Pixel Perfect (whole frame) and the 360x240 of Scaled (two side strips). **Only in VIEW: ORIGINAL**
  (owner, 2026-10-17): in WIDE the cell is locked (`---`, dim, a tap says where it works) and nothing is
  drawn whatever `gba_frame` holds (`Port_Config_GbaFrameActive`). The mzm colour and
  vignette options are left out (COLOURS is ours; the owner had no use for the vignette). Code:
  `platform/3ds/source/gba_look_3ds.c`, called from both top presenters in `platform_gpu_3ds.c`.
  The artwork is mzm's, shipped as `romfs:/gba-look-frame.lz4` (not in the binary: the read-only
  segment is pinned below `.data` at 0x630000 in `3dsx.ld` and 115 KB more overflowed it).
  *Done when:* the owner has seen both on the console in Pixel Perfect, Scaled/Original and Full View,
  in 3D too (**done on the New 3DS, 2026-10-17**), and the frame rate does not drop on the Old 3DS with
  the grid on HIGH (still to see).

- [x] **P4.8** WIDE + AI (owner's request, 2026-10-17; from `../mzm`).
  *Spec:* the scene extensions made with a generative AI (the portal's sides, the title's forest, the file
  select's cave) are shown only when the player wants them. OPTIONS -> VIEW cycles WIDE, **WIDE + AI**,
  ORIGINAL (`wide_ai` in `tmc3ds.ini`, kept apart from the aspect so every "is it WIDE" check stays as it
  is); plain WIDE draws none of them (the title, file select and portal fall back to their 240x160 screens).
  The first time WIDE is on, the bottom screen asks once whether to show them (a modal like the other
  prompts, in the five UI languages, `wide_ai_asked`; installs that predate it get the question once).
  *Done when:* the owner sees the question, answers it and sees the setting change the scenes. **Done on
  the New 3DS, 2026-10-17.** mzm's FTP-install progress bar was already here since 2026-10-14.

- [ ] **P4.9** The neighbouring room drawn beyond the room's edge in Full/Wide (owner's request, 2026-10-18).
  *Spec:* where a room borders another (the overworld's room grid, `RoomHeader.map_x/map_y/pixel_width/
  pixel_height` in `include/area.h`), the part of the 400x240 (or 266x160) view that lies past the room's
  edge shows the neighbouring room's two tile layers instead of black or a repeat of the border, at rest
  and while walking, in every area where it holds (outdoors first; interiors only where rooms really
  abut). No entities, no collision, no logic of the neighbour: only its map. The port already keeps the
  *outgoing* room's tiles for a border scroll (`docs/widescreen-streaming.md`, `Port_Widescreen_BeginScroll`).
  *Open points:* a neighbour with another `tileSet_id`/palette cannot share the layer's VRAM tiles (leave it
  as it is now); animated tiles; the Full View camera limits; the cost on the Old 3DS.
  *Probe (branch `feat/wide-neighbour-rooms`, 2026-10-18):* one outdoor room, what it costs and which rooms
  fail; findings in the decisions log.
  *Done when:* the owner walks the overworld and sees the neighbour beyond every edge that has one, with no
  pop at the scroll and no frame-rate loss.

## Phase 5: stereoscopic 3D

- [ ] **P5.1** Stereoscopic 3D on chosen layers (owner's request, 2026-10-14).
  *Spec:* the game stays 2D; layers over the playfield (fog, the leaves over the Minish village)
  can be given a depth so they sit in front of the screen in 3D. Off by default; what gets a
  depth, and when, is the owner's call. First the structure (`docs/stereo-3d.md`): the setting
  `stereo_3d` (OFF / ON / TEST), a picture per eye from the GPU renderer, the presenter drawing
  each eye; then depths scene by scene as the owner asks, the edges a moved layer uncovers, and an
  OPTIONS cell. *Done when:* the owner sees the layers they chose in front of the screen on their
  New 3DS, at 60 fps on both models with the slider up.
  *Done 2026-10-17, New 3DS, in the release* (table in `docs/stereo-3d.md`): the sun rays of the Minish
  Woods, the leaves of the Minish Paths, the title's rays (with the logo and sword in front of them), the
  file select (sprites, hearts, languages, name letters), the Nintendo and Capcom logos, the boot splash
  and the numbers box (SHOW FPS, at the HUD's depth). **Open:** the Minish Village (`feat/village-3d`:
  houses back, hanging fruits), more scenes as the owner asks, the edges a moved layer uncovers, an OPTIONS
  cell, and the cost on the Old 3DS (it has no slider, so only a New 3DS shows it).

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
- **2026-10-13** Full View border scrolls (issue #11): the 400x240 window could stick out of the
  incoming room across the scroll (03/05 -> 03/04: the room below is narrower, a black strip on the
  right until the camera caught up afterwards). The camera now first moves across the scroll to
  the nearest position that fits both rooms (inside the outgoing one, which has the tiles), within
  the retail duration (4 px a tick, at most 20 ticks), then scrolls straight; when the incoming
  room is wider no line-up is needed. Only Full View does it: it only scrolls into rooms of at
  least 400x240, so a position that fits exists unless the rooms barely overlap.
- **2026-10-13** A save state loaded into a build whose globals had moved crashed in a house
  door's script (Luma dump 4: GetNextScriptCommandHalfword, houseDoorExterior.c:244). Every entity
  keeps its script context in `gEntityScriptCtxTable` (relocated, a kInner holder) and in its own
  field at +0x84, a pointer into the middle of `gScriptExecutionContextArray` that no rule covered.
  New states relocate it (`kInnerTargets`); on load every live entity's field is put back from
  the table, which also repairs the states already on the SD card.
- **2026-10-13** The R prompt follows the HUD: the button at the right, the game's label art
  (in the game's language) over it to its left, measured on a scratch strip first because
  `Port_SecondScreenTheme_DrawActionLabel` places by its centre.
- **2026-10-13** The iris fade in a wide view is drawn by the port, not through the GBA window
  path: WIN0H is 8-bit and the game's table has 160 lines. The GPU renderer draws it as a mask
  (backdrop quads outside the circle over a windowless frame), which is exact for the iris's
  settings (inside everything, outside the backdrop): a band per line through the window path
  cost the Old 3DS ~40 ms of building and a scissored full-screen quad per run ~99 ms of GPU
  wait. 150, where the game ends the iris, is scaled to the farthest corner from the circle's
  centre, so an off-centre iris (Ezlo near a room's edge) does not jump at its end.
- **2026-10-13** The updater orders `X.Y.Z-dev` < `X.Y.Z BETA` < `X.Y.Z`. The first test
  (2026-10-11) had dev and beta level for no stated reason; with the release branch renamed after
  every tag a dev build of X.Y.Z always predates its tag, so it is offered the beta.
- **2026-10-13** The install-only FTP server of production builds (`dev-ftp.txt`) is anonymous,
  on the owner's word ("solo se está compartiendo la carpeta update"), after being told that the
  title-ID check only stops a CIA of another app: anyone on the network can install a CIA built
  with our title ID while it runs. `../mzm` has a login for it; a hardcoded password was refused
  by Claude Code's permission check here and the owner chose none. The docs say to keep the file
  off on untrusted networks.
- **2026-10-13** Fixes to the updater and to shared tools go to `../mzm` and `../sm-3ds` as well
  when they have the same bug (owner, 2026-10-13): the updater's BETA label went to all three.
- **2026-10-13** The bottom-screen painter runs on core 1 on the Old 3DS (BOTTOM CORE = AUTO): on
  core 0 the main thread starved it to 350-550 ms a paint in Full View, the root of the bottom
  screen's lag (#22); on core 1 it is about 70 ms with the same frame rate. The New 3DS keeps core 0
  until measured there.
- **2026-10-13** Cutscenes the GBA drew at 240x160 can be widened only where the picture exists:
  the portal's falling scene (P4.3) is 256x320, so its sides come from a picture the owner made
  with generative AI from a screen dump (palette kept, centre untouched). It is read from the SD
  card (`portal-cutscene-strips.bin`), never shipped: it is derived from the game's art. Without
  it the sides are black.
- **2026-10-13** The generated scene extensions ship after all (owner, so players have them):
  only the generated parts, the original area transparent (`platform/3ds/assets/wide-scenes/`),
  converted into the romfs; a file of the same name in the data folder replaces them. This
  replaces the entry above that kept them on the SD card only. Dumps, templates and whole
  pictures (with the game's art) stay off the repository.
- **2026-10-14** The title's light rays are extended by copying the rays themselves, not by a
  generative AI (owner): every ray is at 45 degrees, its upper half repeats along its diagonal, so
  each cut diagonal is continued to the view's edge with a stretch of it; the gaps the GBA's screen
  never showed (left, lower right) get copies of whole rays (owner, same day). Unlike the forest,
  the added pixels are the ROM's own art, so nothing ships: the game computes the extension at the
  title from the BG0 it loaded and writes it into VRAM as tiles and a 512x256 map
  (`title_rays_3ds.c`), which both renderers draw as BG0 with the palette rotation, blend and fades.
  `tools/scene-extend/extend_rays.py` makes the same picture on the PC (preview, placements).
  GPU CHECK on the title: 0 structural; the one-step blend differences (7000-14000 pixels, by
  phase of the rays' blend) were there before the change.
- **2026-10-14** Full View in every room (owner, P4.6): the policy no longer asks for a 400x240 room;
  the camera of a smaller room rests outside it, centred, and the map shadows already give black
  outside the room. Interiors stay at 1x. The Minish Paths corridor is still to check.
- **2026-10-14** Everything through the GPU renderer (owner): the software renderer is kept as the
  reference for GPU CHECK and as the fallback, but new screens (the file select at 400x240) are
  built for the GPU only. The title's slowness was the GPU path itself, not a fallback: mode-1
  frames kept every layer off the view rings, and the scaled sword was a quad a tile rebuilt each
  frame. The `192.168.1.141` console reports itself as an Old 3DS.
- **2026-10-14** `v0.2.0` is the stable release after `v0.1.2` (the owner named the number: a minor
  bump for the 400x240 title, file select and portal cutscene). Tagged on `main`; the release line
  is now `release/v0.2.1`.
- **2026-10-14** Stereo 3D is back in scope (owner): the game stays 2D, chosen layers in front of
  the screen. Off unless the owner asks; the structure first, depths later, scene by scene (P5.1).
  The goal line changed from "no stereo 3D" to that.
- **2026-10-14** Rooms that swap building graphics by zone (Minish village, Hyrule Town) keep every
  set on the port side for Full View (#26), rather than shrinking the view there: the zones were
  sized for a 240x160 screen, and bigger zones would not help since every set uses the same tile
  numbers (the owner asked). 60 fps kept: zone cells are decoded once into the view rings.
- **2026-10-13** RetroAchievements reads the game at its GBA addresses through a table generated
  from `linker.ld` and the ELF (`tools/gen_ra_map.py`), not a hand list as `../mzm` started with:
  every global the original keeps at a fixed place is covered at once, and the ARM32 structs keep
  the GBA layout. The ROM is hashed from the file (the port patches `gRomData`). rcheevos and
  `retro_ach.c` are built `-Os` to stay under the pinned read-only segment instead of raising the
  pin, which would refuse every older save state.
- **2026-10-15** Stereo 3D is always on (owner): only the console's 3D slider turns it off, so
  the TEST mode is gone and `stereo_3d_on` (default 1) is the one switch (an older `stereo_3d=0`
  in `tmc3ds.ini` was the former default, not a choice, hence the new key). `3dsx.ld` pins
  moved up 0x10000 (.data 0x630000, .bss 0x640000) for rcheevos: format 1 save states of older
  builds no longer load; format 2 is unaffected.
- **2026-10-15** RetroAchievements does not support the clean European ROM: the set (game 559)
  supports the USA hash and hacks of it ("European Backport Edition" among them), lists the EU hash
  as "needs a patch to be compatible", and the server answers it with the "Unsupported Game
  Version" pseudo-game (`1200000000 + 559`, one achievement). The port hashes the file correctly
  (md5 equals the file's), so it is not a port bug. The owner will, at some point, ask RA to add
  the European hash. To test the mapping on the EU ROM the owner used a test account
  (`tinaut1986test`) and the USA md5 in the ini's `rom_md5`: the set loaded and "A New Quest"
  unlocked, which supports the table from `linker.ld` being right for EU too (its data variables
  have the same addresses in both regions: the EU/USA split in the linker script is only the
  functions copied to IWRAM, which `gen_ra_map.py` skips). Not proven for the whole set.
- **2026-10-15** The FTP install notice (percent and bar for the upload via `ALLO`, the install, and
  the 10 s countdown) is only the interface: the 10 s wait before the relaunch, which stops the
  loader crash (#4), was already in the release. Checked by the owner on the 2DS; merged.
- **2026-10-15** Lesson: poking an inventory bit on the owner's running game leaves traces the
  game does not undo (the sword was auto-assigned to X and stayed after the bit was cleared). Ask
  first; use a loaded state or a build with the change instead.
- **2026-10-15** Tried to give RA a hash of our own patched ROM: Prof9's EU Backport source
  (ARMIPS, built locally, `Kingcom/armips`) applied to the clean EU ROM touches 1159 bytes (bomb
  bag, Eenie, ocarina: nothing else) and hashes to `fbda34fc...`, which RA does not know ("Unknown
  game"); RA's "European Backport Edition" hash is `626a6f11...`. So no patch-in-memory work was
  started (P2.9 not opened). The `rom_md5` trick (a known hash in the ini) still works only for
  test accounts and is not a feature.
- **2026-10-15** For the clean European ROM the port tells RA the backport edition's hash
  (`626a6f11...`) (owner's call, softcore only). RA's BPS (RAPatches `GBA/Fix/559`) applied to the
  owner's ROM gives exactly that hash, so it is the same content the port already has natively;
  Prof9's source built with ARMIPS differs from it by two bytes (md5 `fbda34fc...`, unknown to
  RA). The cleaner way, applying the BPS in memory and hashing the result, was not needed. The
  European ROM with the patch applied (SHA-1 `9a6d87e7...`) is accepted as EU (3DS: header code only;
  PC picker: hash list) and was run on the 2DS. The bomb bag offer is in the player's language
  from Prof9's strings (issue #29).
- **2026-10-15** Room-border scrolls in Full View now include interiors (#11 had been fixed for
  exteriors only: the castle's rooms 80/01 <-> 80/00 still dropped to 266x160 for the whole
  scroll). `Port_Widescreen_BeginScroll` no longer asks for an exterior; the incoming room must
  still be at least 400x240 (a smaller one keeps the old 266 scroll). Recorded on the 2DS,
  down and up: every frame 400x240. The incoming room may now be smaller than the view too
  (Hyrule Castle's room 80/02 is ~270 wide): the scroll lines up on, and ends at, the centred
  position (black borders), instead of falling back to 266x160 (owner's report, 2026-10-15).
- **2026-10-15** The new game's opening shot (AuxCutscene 1: Hyrule Field room 1 with Link's house,
  `sCutsceneData[1]`) takes the wide view. The port treated every active subtask as a fixed 240x160
  canvas (`Port_WidescreenFixedCanvasSubtask`); this one draws the world as in play, so it is
  exempted in its update phase (`gUI.lastState == SUBTASK_AUXCUTSCENE`, `field_0x3 == 1`,
  `nextToLoad == 2`). The other aux cutscenes (Ezlo, castle) are fixed pictures and stay fixed.
  The entry and exit fades of that subtask are still a fixed canvas: the view steps at both ends.
- **2026-10-15** The updater reads `/releases?per_page=100` for both channels
  (`Update_ParseReleases`): + BETAS offers the newest of betas and stable releases (a pre-release
  compared as `X.Y.Z-beta`), STABLE the newest stable one, and the notes join those of every
  release of the channel newer than the installed build, newest first; an unusable old release in
  the list is skipped. The sibling ports already did both (audit, see Start here).
- **2026-10-15** Map switches moved out of OPTIONS onto the map (owner's request): MAP FOLLOW and
  CREST PINS are small chips in the bottom corners of the overworld map, FLOOR BACK a chip in the
  dungeon map; lit when on. Same `SS_SET_*` settings and handlers. Branch `feat/map-toggle-chips`.
- **2026-10-15** The right-edge glitch (#30) was investigated and not found in the displayed
  framebuffer: the scene recorder reads it through `GSPGPU_ImportDisplayCaptureInfo`, so a tearing
  at presentation (a frame swapped mid-scanout) is invisible to it, as is anything that is only
  on the LCD. VBLANK LOCK (phase-locked presentation) did not change it; stereo does not apply to
  a 2DS. Left: a CPU/GPU race on the retained map slices (FRAME SYNC helps partly) or a regression
  since dev.18. The recorder also reports colour: the owner's COLOURS toggle mid-recording changed
  the palette between frames, which looked like a glitch.
- **2026-10-15** Lesson: three Luma dumps in one evening (14, 15, 16) were one cause seen three
  ways: states restored in builds whose globals had moved or whose port-side tables are not part of
  a state. Test with states made in the build under test; do not load older ones. Symbolising a
  dump needs the ELF of the *installed* commit, not the working tree's (rebuild that commit:
  `git checkout --detach <commit>`, build, `arm-none-eabi-addr2line`).
- **2026-10-15** #31 was not a layout the states could not follow but stale words a state
  inherits: loading a state puts every word back and the next save writes it again, so the heads
  of the empty entity lists kept addresses from before the 0x10000 move of `.bss` through every
  later state, faithfully "relocated" into `gMapTop`. Leaving a room walked them into tile data
  (dumps 15 to 19; the NULL script context of dump 15/17 was the same walk). A relocation rule
  cannot fix a word that already looks like a pointer to something else, so the load now repairs
  the structures of known shape (`RepairEntityLists`). The layout fingerprint proposed in #31 was
  dropped: refusing a state loses the player's place, the owner's rule is that states load in any
  build. Lesson: a crash after loading a "new" state can come from the old state it was played
  from; check the state's lists (`gEntityLists`, pools) before blaming the build.
- **2026-10-15** v0.3.0 went straight to stable, no beta, at the owner's request ("súbelo todo
  como estable, confío"): the four topic branches merged after the owner's checks of the test
  builds and of the #31 repair on the 2DS; no production build was run on the consoles first.
  #30 is not a regression: room changes were worse before (owner), so it did not hold the release.
- **2026-10-17** GBA grid and frame (P4.7): taken from `../mzm`, whose third effect (the colour
  mix) is our COLOURS option and whose vignette the owner did not want. The grid mask is an 8-bit
  texture (128 KB) with a constant black tint, not mzm's 512 KB RGBA one; the frame is read from the
  romfs when first needed and unpacked into its own texture (512 KB), so a player who never turns it
  on pays nothing. First attempt embedded the artwork as a C array like mzm: the link failed on
  `3dsx.ld`'s pin (read-only data 0x6411a1 against 0x630000). Raising the pin would have moved
  `.data`/`.bss` again and cost the old format-1 states once more; the romfs avoids that.
- **2026-10-17** WIDE + AI (owner, from `../mzm`): plain WIDE shows no generated art, so players who do not
  want it keep a Full View without it (the title, the file select and the portal fall back to their 240x160
  screens or black sides); WIDE + AI is the aspect's second stop, and a one-time question on the bottom
  screen asks at first launch of WIDE (existing installs get it once). The FTP install progress bar of mzm
  (percent, install, 10 s countdown) was already here since 2026-10-14. `feat/wide-ai-ask` and `feat/gba-look`
  both edit the settings' load chain in `port_config_3ds.c`: the second one merged resolves a small conflict.
- **2026-10-17** Merging: the owner tests branches through test builds I make in a throwaway worktree
  (`../zelda-tmc-3ds-test`, detached, the branches merged there by hand), so what runs on the console is not
  on any branch and the owner assumed it was already in the release. Rule from now: when the owner says a
  branch works, commit and `--no-ff` merge it into the release line at once (and push when asked), then
  delete it; ask "which branches are you testing?" before building a combined test.
- **2026-10-17** The GBA frame is only for VIEW: ORIGINAL and the grid covers the whole screen in WIDE (owner,
  after seeing the CAPCOM logo's white border without lines): in WIDE the border is the game's own backdrop,
  so the lines go on across it with the picture's step and phase (floor division, rows and columns outside
  the picture continue the pattern); the frame has no border to fill there.
- **2026-10-17** Stereo scenes are detected from the registers the shown frame was drawn from (`gIoMem`, as
  `Port_Widescreen_TitleForestShown` does), not from `gScreen`: the first detection of the title's rays read
  `gScreen` and never fired (the log's `stereo: scenes N` line, debug builds, is the check: the title must
  give 1, the logos 16, the file select 8). On the title the logo (sprites) and the sword (BG2) go to 8 px
  against the rays' 4, because the PPU draws them in front of the rays and a depth behind them looked wrong.
- **2026-10-17** The leaf blocks of the Minish Village sit on BG2 with the ground and the PPU can only give a
  depth to a whole layer, so `feat/village-3d` copies their cells (palette bank 5, plus the dark outline tiles
  521, 526, 527, 533, 782, 783 touching them) to BG3 at priority 1 while 3D is on. The copy must be made in
  `DispCtrlSet` (the moment the registers are latched), not at the VBlank hook: the game streams new
  columns into BG2 after the hook and the layer lagged a tick behind (bars and hairlines while walking).
  Palette bank 6 is the ground: including it put the whole ground over Link.
- **2026-10-17** The boot splash is drawn straight into the gfx framebuffers: libctru's
  `gfxScreenSwapBuffers(screen, hasStereo)` takes **hasStereo**, not "immediate"; passing false copies the
  left image to the right eye, so a splash drawn per eye stayed flat (it cost four installs to find: the log
  showed 3D on and distinct buffers). The logo was also off-centre (the PNG has 4 px of margin at the left,
  24 at the right): `tools/make_splash.py` cuts it to its bounds and centres it.
- **2026-10-17** `upstream`'s push URL was not `DISABLED` in this clone although CLAUDE.md says it is; set it
  again (`git remote set-url --push upstream DISABLED`). The owner does not want anything pushed there.
- **2026-10-17** The village intro's five Minish (`ForestMinish` `type2` 39 to 43, script_ForestMinish5..9) spawn
  just outside the GBA's 240x160 view around Link, which a wider view shows, so they popped in. In a wider view
  (`Port_Widescreen_HudTargetViewWidth() > 240` or height > 160: the view is only 266 wide when they spawn and
  becomes 400x240 later, so the real width cannot be read) they start 96 px further out and walk the extra
  distance; the script waits for them by sync flags. The one from the shoe house's door comes down the path on
  the right instead (x 0x278), and `ScriptCommand_MoveTo` sends him back up it. Non-wide play is untouched.
  The entities load with the room, so a save state made mid-scene keeps the old positions: to test, clear the
  local flag (gSave.flags bit 0x28d, `poke 0x6e9a09 00` with this ELF), leave the room and come back.
- **2026-10-17** #20 done: the heart in use is drawn as the game's 16x16 face of sprite 322 (`DrawHeartInUse`),
  in both sidebars (`bottom_compact_3ds.inc` is the one on the console). The numbers box has a fifth line in
  debug builds, `C<n>`: frames the software renderer drew in the last second (`tmc3ds_build_config.h` must be
  included for `TMC3DS_DEBUG_TOOLS` to be seen); dialogues showed C0, so P3.4's old claim needs rechecking.
- **2026-10-17** Narrow rooms in Full View (area 0x20 room 09): the shadow layers filled what is not the room (and
  left of the light rays' picture) with entry 0, which draws tile 0; that tile is blank in most tilesets but
  was a dither in this one, and showed as noise beside the room. `Port_WidescreenBlankEntry` picks the first
  fully transparent tile of the layer's own character block (up to 1024 tiles, into the next blocks).
- **2026-10-17** Minish village cells outside every zone region (`port_zone_tiles.c`): the game only swaps graphics
  for the regions on screen, so a gap cell next to a zone never showed before that zone's set was loaded; the
  400x240 view showed it as a block of garbage that came and went with the camera. Gap cells within 128 px of a
  region take its set (Hyrule Town unchanged).
- **2026-10-17** The hold of the faded top frame at a room change (`Port_Widescreen_3DSHoldTop`) was only for
  exteriors; every room is a Full View candidate since P4.6, so doors into interiors hold it too. One frame at
  240x160 before the hold remains, in both.
- **2026-10-17** A state saved inside a subtask loses nothing now: `Subtask_FadeIn` moves the room's entity lists
  into `gEntityListsBackup`, and the load's list repair freed those entities as strays (the NPC whose script
  gives Link back his controls after a map hint: Link stayed in `controlMode` 3). It walks the backup while a
  subtask is up and logs each stray it frees. Details in `docs/save-states.md`.
- **2026-10-17** 3D of the map screens (scene bit 32, `windowOutsideControl == 0x3d3f` inside a subtask, so the
  map hints and the pause menu's map): the grey and its frame (BG2) on the screen plane with the backdrop (which
  cannot move: moving the frame alone made it slide against the green), the unlocked map (BG1) at 2 and the
  markers (sprites) at 5. The OBJ-window sprites, which cut the map out, stay with BG2 (`Stereo3DS_ObjWindowFollows`):
  moved with BG1 they left a 2 px line of backdrop at its edges, because the renderer cuts BG2 into batches along the
  window's edges as the game drew them. Found with both eyes captured (`FORCE 3D`, `top-right.png`).
- **2026-10-17** Debug views after `../mzm` and `../sm-3ds`: PLANE TINT (LAYERS, DEPTH; `PortPpuGpu3DS_SetTintMode`),
  FORCE 3D (`Stereo3DS_SetForce`) and the right eye in `tmc-link.py shot`. Not saved. The depth view reads
  `Stereo3DS_LayerDepth`, which works with the slider down.
- **2026-10-17** The bottom map shows what the game's map shows: the game art of the second screen goes through the
  COLOURS curve (`port_second_screen_color.c`; the theme's chrome does not, cached once), the hints are drawn over
  the unrevealed regions (the game's WINOUT 0x3d3f keeps sprites on inside the OBJ window; the port had drawn
  the grey last "so hidden terrain cannot leak"), and hints and Link swap every 32 game frames by bit 5 of
  `gMain.ticks`, published in the snapshot (the bottom's own tick runs at 20 Hz, three times slower). Owner's
  request; the repaint predicate follows the phase.
- **2026-10-17** `tools/scene-extend/extract_map_room.py` draws a room that is a scrolling map (houses, shrines)
  from a dump's `map-*-special.bin`; `extract_bg.py` only finds the 32x32 window of such a room's BGs. The
  owner's outpainting material for area 0x4A is in `~/Imágenes/outpaint-area-4A/` (template with black bands,
  mask, marked exterior reference, prompt).
- **2026-10-17** Dark rooms stay in Full View (#32): the light around Link is WIN0 from the game's 8-bit HBlank
  table, which Full View counted as a fixed canvas and dropped to 266x160. The light manager notes its circle and the
  GPU renderer takes the view-wide lines as WIN0 itself (`PpuGpu3DSFrameView::win0Lines`), not as the iris's backdrop
  mask, because the dark room's window settings differ (inside: no BG3). 60 fps on the New 3DS with 3D off; with 3D
  up the shrine is under 60 in both views, a separate matter. Details in `docs/gpu-fade.md`.
- **2026-10-18** Stereo 3D drew the whole frame twice on the GPU, so the right eye doubled its cost. Measured on the
  New 3DS (perf recorder, FPS overlay's G = `C3D_GetDrawingTime`, `gPpuGpu3DSEyesWhole` to switch paths on the same
  state), Full View with #32: the dark room 0x48/0x10 takes G 9.0 ms in 2D (60 fps), 17.1 ms in 3D (40 fps); room
  0x0B 6.0 / 11.0 (60 both); the Minish Woods with its rays 7.4 / 13.8 (58). The frame is serial: the CPU builds the
  commands only after `C3D_FrameBegin` has waited for the previous GPU frame (the vertex, index and atlas buffers
  are single, `port_ppu_3ds.c`), so the interval is the build (3.0 ms in the dark room, its 21 bands and 300
  batches; ~1 ms elsewhere) plus ~3 ms of recording and presenting plus the GPU frame. Now the layers on the screen
  plane are drawn once, the right eye copies them and each eye draws only its layers in front
  (`DrawEyesOverBase`, docs/stereo-3d.md); a frame with nothing in front is drawn once for both. Dark room: G 12.6
  ms, 54 fps; 0x0B 9.1; the Woods 11.6 and 60 fps. Both eyes compared pixel by pixel with the old path from the
  same state in eight scenes (dark room, 0x0B, Minish Woods' rays, Minish Paths' leaves, areas 0x03, 0x15, 0x22,
  0x80): identical; GPU CHECK 0 in the dark room. What the dark room still pays in 3D over 2D (~3.6 ms): the copy
  (~0.5), BG0 at priority 0 drawn per eye although transparent (the HUD layer is a full view ring), and the
  presenter's right eye (clear, quad, GBA grid, transfer, ~2). 60 there needs the build overlapped with the GPU
  (double-buffered command buffers and atlas), or a lighter dark room; the overlap came the same day (next entry). The narrow
  266x160 view (before #32) was worse: no view rings, 7.4 ms of build, 45 fps.
- **2026-10-18** The GPU frame is now built before `C3D_FrameBegin` waits for the previous one (`docs/gpu-build-ahead.md`).
  Everything the build wrote that the GPU reads: the vertex buffer (now two; a frame is built into the one the last
  drawn frame does not use, and map slices kept across frames are copied from the other buffer when it has a newer
  build of them, tracked by a generation per slice and buffer), the atlas and the eight view rings (now built into
  heap copies, 2.5 MiB, whose dirty slots and cells `PortPpuGpu3DS_Commit` copies into the textures after the wait;
  the rings record each written cell so a vertical scroll copies 64 cells, not the whole span), and the file
  select's indexed picture (refreshed in the commit). Indices are written once. Without the memory the old serial
  order stays. New 3DS, same build and state, `poke gPpuGpu3DSSerialBuild` to switch: dark room 0x48/0x10 in 3D
  40.1 -> 45.4 fps on `release/v0.3.2`, and **53.7 -> 59.8 fps with `perf/stereo-3d-deepwood`** (wait 11.5 -> 0.03 ms);
  in 2D, room 0x0B in 2D/3D and area 0x00 in 3D, 60 either way, build time unchanged. GPU CHECK as without the
  change (0 in 0x48/0x10 and 0x0B also while walking; areas 0x00 and 0x11 differ by the same 176-208 and 15126 pixels
  in the build without it, so those are older). Both eyes clean (FORCE 3D, `shot`). Tried and dropped: handing the GPU
  the recorded commands every K draws (`C3D_FrameSplit`): no gain (44.6-45.4 fps, the GPU is the limit), and every
  split is an entry of citro3d's fixed-size GX queue: every 32 draws of a 3D frame filled it and libctru's
  `svcBreak` froze the New 3DS (Luma dump: prefetch abort, IFSR 0x2, under `C3D_FrameSplit` from
  `PlatformGpu3DS_EndBottom`); the owner had to reboot it. Issue #30's suspicion (slices rewritten while the GPU
  reads them) cannot have happened in the old order, which waited before every write; the new order keeps that
  rule by construction, so #30 needs another cause.
- **2026-10-18** The Minish Shrine's rolling barrel (area 0x48, room 0x20) ran at 26 fps on the New 3DS because the GPU
  never drew it. The barrel is a 512x512 wrapping affine BG2 whose PA and reference change every line (HDMA): 139
  one-line bands. The affine ring took only <=256x256 without wrap, so each band emitted a quad a tile, 68980 vertices
  against 65536, and the frame was drawn in software after 25 ms of discarded GPU work. Now: the tile path emits only
  the rows of the band's own lines; a 512x512 wrapping BG2 scaled by PA/PD is one quad a band and half from two rings
  (BG2's rows 0-31 and 32-63 in rings 4 and 5, the texture's repeat wrapping the columns; bands whose reference does not
  follow PD are cut into lines, wide spans into quads of at most two textures); consecutive batches that draw alike are
  merged (369 -> 99); and a failed build backs the GPU off for 8..60 frames (the SD log counts failures by reason).
  Result: 26 -> 59.8 fps, every frame on the GPU. Blending needed `fold_covered_alpha_layers`: with BLDCNT 0x3456 BG2 and
  the sprites are both first and second targets under BG1, which blends over them; the GBA blends only the top pixel, so
  the layers underneath are drawn plain (the GPU drew layer after layer and BG1 blended with an already hazed BG2). Seen
  as a light veil over the barrel; compared with the CPU at the same state (99.5 % of the pixels within 16/255).
  Not fully verified: other rooms with the same blend combination, and the Old 3DS.
- **2026-10-18** Leaving the barrel in Full View drew noise over the 400x240 view, on the CPU as well: the exit callback
  runs when the door fade starts, so the substate has left `GAMEMAIN_BARRELUPDATE` while the barrel's affine picture
  (mode 1, HDMA still on) fades out, and `Port_WidescreenFixedCanvasSubtask` no longer called it a fixed canvas; the
  maps the game clears at that point were drawn as a normal wide room. The original aspect never showed it. The
  fixed canvas now also holds in area `AREA_DEEPWOOD_SHRINE` while the display mode is 1 and a fade is active, which
  gives the plain fade to white. The bars beside a 240x160 view now take the same fade as the picture (the shown
  snapshot, or the GPU's weight and level, quantized to the picture's 5-bit step): they ran a frame ahead and one step
  darker on the plateau. Compared by pixels in a scene recording (`scenerec`), GPU renderer, Full View.
- **2026-10-18** `v0.3.2` went beta first, then stable. Promotion: `main` was fast to take it (`git merge --no-ff v0.3.2`,
  push `main`), then the release workflow was dispatched on the tag (channel Release, debug tools off, the release
  page flips from pre-release to Latest); valid because nothing was committed after the tag. The tag's
  `docs/release-notes/v0.3.2.md` only lists what is new since the `v0.3.1` beta, and a STABLE-channel updater skips
  pre-release pages, so a player on `v0.3.0` would miss `v0.3.1`'s notes: the notes block of the release **body** was
  rewritten with `gh release edit` to six lines covering `v0.3.1` + `v0.3.2` (a commit would have invalidated the
  promotion). The file in the repo keeps the beta-only list. `../sm-3ds` fixed this in the updater itself
  (`10aa48d`); ours still needs the port (Status, "Start here" 1).
- **2026-10-18** The in-game updater, three changes checked by the owner on the New 3DS (a build of the branch with a
  fake older version, `TMC3DS_VERSION=0.3.0-dev.99` or `0.0.0`, sent over FTP so the updater offers `v0.3.2`):
  (1) AUTO UPDATE (`auto_update` in `tmc3ds.ini`, on by default; `../sm-3ds` has the same): the check at boot raises a
  LATER / INSTALL window over every tab with WHAT'S NEW (the changelog); with it off nothing is checked until CHECK is
  pressed. The window shows a bar for the download, the install and the 10 s wait, with the percentage (seconds on
  the wait) drawn inside it and recoloured pixel by pixel as it fills. Its repaint has to be asked for
  (`Port_SecondScreen_3DS_NeedsPeriodicRefresh`): the bottom screen skips unchanged frames, and the window sat at 0%.
  (2) The game relaunches 10 s after a CIA update (`Platform3DS_RequestRelaunch`, the FTP install's path); it used to
  close to HOME, inherited from upstream, and an immediate chainload faults the loader (issue #4). A 3DSX update still
  closes at once. (3) The notes shown after a check cover the betas on the way to the newest stable on the STABLE
  channel (`../sm-3ds` `10aa48d`), and with no update waiting the whole channel's changelog, as in `../mzm`/`../sm-3ds`.
  `tools/ui-preview` builds again and takes `UPDATE_PROMPT=ask|work|restart|error`, `NO_WIDE_ASK=1` and `UILANG=2..5`.
- **2026-10-18** P4.9 probe (branch `feat/wide-neighbour-rooms`, owner's EU ROM, room 0x15 of the Deepwood Shrine's
  area 0x48, Full View): the neighbouring room's tiles now fill the black beside a narrow room.
  **How:** `RoomResInfo` gives every room of the area its world rectangle and its `tileSet`/`tiles` definitions;
  `port/port_neighbour_rooms.c` builds, 8 and 120 ticks after a room starts, a window of 8x8 entries around the
  room for each layer (the rooms that overlap it and share both definitions: their map chain is decompressed
  from the ROM, `LZ77UnCompWram`, and expanded with the current layer's `subTiles`, as
  `RenderMapLayerToSubTileMap` does), and `Port_WidescreenShadow_Populate` reads it where it used to write the
  blank entry (outside the room's columns and rows). **Cost:** a few thousand cell lookups per room load and
  a table read per out-of-room cell per frame; the overlay's L/G times did not move (L 0.3-0.4 ms, G 7.4-7.6 ms
  on the New 3DS); 74 KB of static (two 128+64 x 96+40 windows plus one 8 KB map). **Checked against the room itself:** the
  first build drew the wrong tiles with black holes (the owner caught it): a room's map is stored with rows of
  its own width and the game spreads them to 64 after loading (`sub_0807C8B0`); the strip read them as rows
  of 64. Read with the room's width, the 64 px left of room 0x15 match room 0x14 (reached on the lily pad with
  the Gust Jar) pixel for pixel but for Link, the lily pad, the debug overlay and the shoreline animation's phase.
  The 64 px right of room 0x15 stay black: no room of the area lies there.
  **Border scrolls** (owner's report: room 0x12 down to 0x15, blowing the lily pad with the Gust Jar): the scroll
  drew only the outgoing room's snapshot and black elsewhere, and the incoming room's window came 8 ticks late.
  Now the scroll's empty cells come from the incoming room's neighbours (`Port_WidescreenScrollTile`), the
  window is built on the tick the room changes and is wide enough for a view still in the outgoing room (a
  whole view each side: 2 x 232 x 160 entries, 148 KB). **The module's statics are left out of save states**
  (`3dsx.ld`, with `port_zone_tiles`): a state saved by a build with another window size restored it as
  garbage and kept the stale room key, so nothing rebuilt it. Seen frame by frame (`rec`): no black but where
  the area has no room (right of 0x12 and 0x15).
  **Camera off centre where neighbours fill the view** (owner's idea, room 0x07 right into 0x09, 272x208: the
  scroll first lined the camera up 16 px lower to centre the short room, leaving a black row below where there
  is no room, while the room above, 0x08, could fill the top): a room smaller than the view on an axis now rests
  at the position, among those that keep it whole in view, whose margin is most covered by drawable
  neighbours (`Port_NeighbourRooms_Rest`, by the rooms' rectangles and tile sets, cached per room); centred on a
  tie or with none, as before. Used at rest (`Port_WidescreenCameraRest*ForMode`), for the scroll's line-up and
  for its end. Recorded: 0x07 -> 0x09 scrolls straight across with no black.
  **Line-up order** (owner's report, room 0x00 down into 0x05): two positions tied for the incoming room (both
  sides covered) and the centred one won, so the line-up moved the view 64 px left first, still in 0x00, where
  nothing lies beside it. A tie now goes to where the camera already is, and the scroll's line-up
  (`Port_NeighbourRooms_LineUp`) picks, among the incoming room's best positions, the one that leaves the least
  black in the outgoing room's view while it lines up. Seen working by the owner.
  **What fails, from the ROM's room tables (`tools/neighbour-survey/survey.py`):** 402 pairs of rooms share an
  edge in the whole game, 379 with the same `tileSet_id` (the 23 others stay black: the Beanstalks 8, the Fortress of
  Winds 8, Hyrule Field 4, and one each in the Minish Village, area 0x47 and the Deepwood Shrine). Counted over the margin
  of a 400x240 view around every room smaller than that, **21.5 % of the margin cells have a neighbour with
  the same tile set, 6.2 % one with another, 72.3 % none**: outdoors (Hyrule Field 78 %, Ruins 74 %, the
  Palace of Winds 73 %, Temple of Droplets 39 %, Hyrule Castle 41 %) it pays, the house interiors almost never
  (0.9 % to 37 % by area, mostly 0-20 %): those need the AI side strips of P4.6, not a neighbour. Not done:
  tiles >= 0x4000 (the game's moved or removed tiles) use their plain index; the zone-swapped rooms (Minish
  village, Hyrule Town) do not carry their variants into the neighbour's cells; scrolls keep the outgoing
  room's tiles; normal Wide (266) is untouched.
