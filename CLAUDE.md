# Project notes for Claude

Native Nintendo 3DS port of **The Legend of Zelda: The Minish Cap**. This is the
owner's fork (`tinaut1986/zelda-tmc-3ds`) of EstebanPdN's port, which is built on
Project Picori (`999sian/tmc`), samyost1's dual-screen Android port and the
`zeldaret/tmc` decompilation. The owner already shipped two ports of the same
kind, Metroid: Zero Mission (`../mzm`) and Super Metroid (`../sm-3ds`); they are
the reference for workflow, debug tools, bottom-screen UI and performance work.

**Start every session by reading [`docs/PLAN.md`](docs/PLAN.md).** It holds the
phase roadmap, the task specs with acceptance criteria, and the decisions log.
The next session starts from that file, not from chat history.

Each fact lives in one place; keep that place current in the same change that alters it:

| What changed | Update |
|---|---|
| A task finished | tick it in PLAN; anything non-obvious goes to the decisions log |
| A task added, changed, split or dropped | its spec in PLAN (IDs are never reused or renumbered); a dropped one stays, struck through, with the reason in the decisions log |
| Priorities (the owner reorders, or a task becomes blocked) | the **Priority** line in PLAN's Status, and why in the decisions log |
| A branch opened, handed to the owner for testing, confirmed or merged | the branch table in PLAN's Status |
| A bug found or fixed | its GitHub issue (what, which commit, what to check); never a copy in PLAN |
| A tool, option, file format or workflow added or changed | its doc (`docs/debug-tools.md`, `platform/3ds/README.md`, this file's Build section) |

PLAN's Status section holds only what no other place records: open branches, what
waits for the owner's check, and the priority order. Do not grow it into a summary of
issues or specs.

## Language

- Chat with the owner in **Spanish (castellano)**.
- All code, comments, identifiers, commit messages and docs: **English**.
- Exception: the inherited code (`src/`, `include/`, `asm/`, `data/`, `port/`
  and `platform/3ds/` as upstream left them) keeps its own style. Do not churn
  it; convert what you touch for another reason.

## Where things come from

| Path | Origin | Notes |
|---|---|---|
| `src/`, `include/`, `asm/`, `data/`, `sound/` | zeldaret/tmc decompilation, via Project Picori | The game. Edited in place where the port needs it. |
| `port/` | Project Picori (PC port) + samyost1 (Android second screen) + EstebanPdN | Platform layer shared with the PC/Android builds: PPU, audio mixer, saves, the bottom-screen compositor (`port_second_screen*.c`). Most of it also builds for PC with `xmake`; keep it compiling there (`#ifdef TMC_3DS` for 3DS-only code). |
| `platform/3ds/` | EstebanPdN, now ours | The 3DS frontend: CMake build, GPU renderer (`port_ppu_gpu_3ds*.c`), presenter (`platform_gpu_3ds.c`), Old 3DS frame pacer, NDSP audio offload, updater, bottom-screen glue. |
| `libs/agbplay_core/` | ipatix's agbplay (LGPLv3) | The m4a music engine. |

Other local checkouts: `../mzm` and `../sm-3ds`, the reference ports.

## Git and GitHub

- **Every push, PR, issue and release goes to `tinaut1986/zelda-tmc-3ds`.** Never
  to EstebanPdN, Project Picori or zeldaret.
  - `origin` = `tinaut1986/zelda-tmc-3ds`, `upstream` = `EstebanPdN/zelda-tmc-3ds`.
  - The push URL of `upstream` is `DISABLED` as a safety net (local config; redo it
    in a fresh clone with `git remote set-url --push upstream DISABLED`).
- `gh repo set-default tinaut1986/zelda-tmc-3ds` has been run
  (`remote.origin.gh-resolved=base`). Re-run it in a fresh clone, otherwise `gh`
  targets the fork parent.
- Issues on `tinaut1986/zelda-tmc-3ds` are used for playtest bugs (they have to be
  enabled in the repository settings first; see PLAN P0.3).
- Taking upstream changes: `git fetch upstream`, merge `upstream/main` into a topic
  branch cut from the active release branch, resolve, and bump
  `platform/3ds/upstream_version.txt` to the upstream version merged. Never rebase
  our history onto theirs.

### Branches

- `main` = stable. Only receives `--no-ff` merges of a finished release branch.
- `release/vX.Y.Z` = the current release line; work accumulates here, and being
  on it means "not stable yet". Active line: see PLAN's Status.
- Topic branches (`feat/...`, `fix/...`, `perf/...`, `chore/...`, `docs/...`) are
  cut from the active release branch and merged back into it with `--no-ff` once
  the owner has checked them on the console. **One topic per branch, always cut from
  the release branch, never from another topic branch**: stacking them makes none
  mergeable on its own. The one exception is a task that truly needs another
  unmerged branch; say so in PLAN's branch table.
- Never commit directly on `main` or `release/*`. Commit only when the owner asks.
- **`main` has no `CLAUDE.md` or `docs/PLAN.md` until the first release merge**: it is
  still upstream's v2.2-E1 and GitHub's default branch, so a fresh clone lands on it
  without these instructions. Check out the active release branch first
  (`git checkout release/vX.Y.Z`; PLAN's Status names it) and cut topic branches from
  there. The files reach `main` with the first `--no-ff` merge of a release branch;
  the owner wants `main` to stay the stable line and the release branch the working
  one (2026-10-12), so the default branch is not switched to the release.

### Versions

Our own numbering, independent of upstream's: `vX.Y.Z`, started at `v0.1.0`.
Minor = milestone, patch = fix round. Ask before a minor bump; never infer one.

- The version is derived from git by `platform/3ds/tools/version.sh`: an exact
  `vX.Y.Z` tag gives `X.Y.Z`; anything on (or cut from) `release/vX.Y.Z` gives
  `X.Y.Z-dev.<commits since main>[.<commits since the release branch>]+<hash>`.
  Nothing to bump by hand.
- **Only three-component tags are ours.** Upstream tags with two components
  (`v2.1`, `v2.2-E1`) and those tags are in this repository too; the version
  script, the release workflow (`v[0-9]+.[0-9]+.[0-9]+`) and `git describe
  --match` all ignore them. Never create a two-component tag.
- The upstream version this fork is based on is `platform/3ds/upstream_version.txt`,
  shown next to ours (boot console, dumps, OPTIONS footer).
- This fork installs as **its own application**: title ID `0x5A22E`
  (`cia/tmc3ds.rsf`; upstream is `0x5A22D`), name "Minish Cap 3DS", data folder
  `sdmc:/3ds/Minish Cap 3DS/` (`source/app_paths_3ds.h`). It never touches the
  upstream app's folder, so both can be installed side by side.

### Releases (installer on GitHub)

`.github/workflows/build-release.yml` builds the CIA on any `vX.Y.Z` tag push (and
on manual dispatch) and publishes a GitHub Release with it and a QR code. No 3DSX:
from the Homebrew Launcher the game does not get enough memory (owner, 2026-10-11);
`build.sh` still makes one locally. The channel depends on whether `main` can reach the tagged commit:

| Built from | Channel |
|---|---|
| Tag on an unmerged `release/*` branch | `Beta` (pre-release) |
| Tag on `main`, or on a commit merged into `main` | `Release` |
| Manual dispatch on any branch | `Beta` |

`.github/workflows/build-3ds.yml` only compiles (debug tools on) for pull requests
and pushes to `main` and `release/**`; it publishes nothing.

**First, for either path:** write `docs/release-notes/<tag>.md` on a topic branch, show it
to the owner and merge it into the release branch **before** the tag exists. Do it unprompted
whenever a tag is about to be made.

**Beta**: tag the release branch, push branch then tag.

```sh
git tag -a v0.1.0 -m "v0.1.0"
git push origin release/v0.1.0
git push origin v0.1.0
```

**Stable**: merge into `main`, tag the merge, push **main before the tag**
(the tag build checks reachability from `origin/main`).

```sh
git checkout main
git merge --no-ff release/v0.1.0
git tag -a v0.1.0 -m "v0.1.0"
git push origin main
git push origin v0.1.0
```

A tag already shipped as a beta is promoted by re-running the workflow from the
Actions tab **on the tag itself** (not on `main`). That is only valid if **nothing was
committed after the beta's tag**; otherwise tag the next version.

### Release notes (shown in the updater)

One file per tag, `docs/release-notes/vX.Y.Z.md` (rules in its README): 3 to 6 short
lines, each starting with `- `, plain text for the player, in English. The CI copies it
into the release body between `<!-- tmc-notes -->` markers; the console's updater shows
that block (`Update_FormatNotes` in `platform/3ds/source/update_manifest.c`; the marker
must stay identical in both places), or the commit list when the block is missing.

A **stable** release lists everything since the previous *stable* tag, betas included; a
**beta** lists only what is new since the previous tag.

### The channel is baked into the binary

The workflow passes `TMC3DS_CHANNEL=beta|release` to `platform/3ds/build.sh`. The
generated `tmc3ds_version.h` then carries `TMC_PORT_IS_BETA` and
`TMC_PORT_VERSION_LABEL` ("X.Y.Z BETA"); `TMC_PORT_VERSION` stays the plain number
(user-agent, file names, comparisons). The updater treats an installed beta as
older than the stable release of its own number, and never offers a beta its own
pre-release page.

### After tagging

After **every** tag, beta or stable, rename the release branch to the next patch at once
(before more work or any build) and delete the old remote branch; otherwise dev builds call
themselves `X.Y.Z-dev.N` and the console offers the already published `X.Y.Z` as an update.
Do the steps one at a time and check with `git ls-remote`.

```sh
git branch -m release/v0.1.0 release/v0.1.1
git branch --unset-upstream
git push -u origin release/v0.1.1
git push origin --delete release/v0.1.0
```

## ROM and legal

- Never commit, package or upload a ROM, extracted assets, a save, or any build that
  contains them. `.gitignore` covers `*.gba`, `*.sav`, `baserom.gba`.
- Supported ROMs: USA (`BZME`, sha1 `b4bd50e4131b027c334547b4524e2dbbd4227130`) and
  Europe (`BZMP`, sha1 `cff199b36ff173fb6faf152653d1bccf87c26fb7`). The ROM is read
  at runtime from `sdmc:/3ds/Minish Cap 3DS/`.
- License: GPL-3.0 (see `LICENSE`, `COPYRIGHT.md`, `THIRD-PARTY-LICENSES.md`), agbplay
  LGPLv3. Keep the notices; the build copies them into the romfs.

## Build

Toolchain in `/opt/devkitpro` (devkitARM, libctru, citro2d/3d). The updater's
curl/mbedTLS/jansson are built once by `platform/3ds/tools/build_update_deps.py`
into `build-3ds/update-deps/` when the portlibs lack them. `makerom` and
`bannertool` are committed in `platform/3ds/tools/bin/` (from `../mzm`).

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
./build_3ds.sh                                  # interactive menu (debug build by default)
./build_3ds.sh --mode debug --ftp 192.168.1.50  # build + upload to /cias/ on the console
./build_3ds.sh --mode prod --no-ftp             # what a stable release is
TMC3DS_DEBUG_TOOLS=1 ./platform/3ds/build.sh    # the bare build, no assistant
```

Outputs: `build-3ds/game/tmc-3ds-v<VERSION>.cia` and `.3dsx`. A full build takes
~1.5 min on 4 cores; the version and the debug-tools switch live in generated
headers (`build-3ds/game/generated/`), so changing either rebuilds only the files
that include them. `./build_3ds.sh` (`platform/3ds/tools/build_3ds.py`, from
`../sm-3ds`) can scan the LAN for the console's FTP server (port 5000) and upload
the CIA as `cias/tmc-3ds-<version>.cia`; the last IP is kept in `.3ds_ftp_ip`
(gitignored). Installing by hand: FBI's FTP server,
`curl -T build-3ds/game/tmc-3ds-v*.cia ftp://<3ds-ip>:5000/cias/`.

`TMC3DS_DEBUG_TOOLS=1` compiles in the DEBUG tab and its tools
(`docs/debug-tools.md`); a plain build leaves them out. CI passes 1 for Beta builds
and 0 for Release ones.

## The console in the loop (for Claude)

A debug build running on the owner's console can be reached from this machine
(same LAN, 192.168.1.x) once the owner turns on **DEBUG -> FTP SERVER**; the cell
shows the console's IP. Nothing needs FBI or closing the game:

```sh
IP=$(cat .3ds_ftp_ip)                       # the last console IP the build assistant used
./build_3ds.sh --install "$IP"              # build, upload, install over the running game, relaunch
curl "ftp://$IP:5000/3ds/Minish%20Cap%203DS/debug/"                         # list the debug files
curl -O "ftp://$IP:5000/3ds/Minish%20Cap%203DS/debug/tmc-perf-0001.csv"     # fetch one
wget -q -r -nH --cut-dirs=3 -P /tmp/tmc-debug "ftp://$IP:5000/3ds/Minish%20Cap%203DS/debug/"
tools/perf-csv/analyze.py /tmp/tmc-debug/debug/tmc-perf-0001.csv
tools/tmc-link/tmc-link.py wait && tools/tmc-link/tmc-link.py run SCRIPT   # drive the game
```

- `tools/tmc-link/tmc-link.py` (docs/debug-tools.md, "Remote control") presses buttons,
  taps, saves and loads save states (`savestate`, `loadstate N`; docs/save-states.md),
  takes screenshots (`shot`, then look at the PNGs), reads state and memory by symbol.
  A state survives rebuilds that only change the port (renderer, UI, platform), so the
  loop is: save a state once, then install, `wait`, `loadstate`, play the scenario, measure. Use it to reproduce and check on the console without
  the owner; say before driving the game while the owner is playing.

- `--install` uploads to `/3ds/Minish Cap 3DS/update/dev.cia` and sends `SITE INSTALL`
  after the transfer (`curl -Q "-SITE INSTALL <path>"`; the leading `-` means "after").
  The game refuses a CIA with another title ID, installs over itself
  (`cia_install_3ds.c`), replies `200` and jumps into the new build
  (`Platform3DS_RequestRelaunch`). The relaunched game turns the FTP server back on by
  itself (`debug_ftp` in `tmc3ds.ini`), so installs and fetches can follow one another;
  give it a few seconds after the relaunch.
- Any unsaved progress is lost on relaunch: say so before installing while the owner plays.
- With two consoles on the network (owner's New 3DS and 2DS) `.3ds_ftp_ip` holds only the last
  one the build assistant used. Pass the address explicitly (`tmc-link.py --ip IP ...`, `curl
  ftp://IP:5000/...`) for the other one, and do not overwrite the file by sending a CIA to
  it with `curl`. Which one is which is in PLAN's Status.
- If the IP is unknown or the connection is refused, ask the owner for the address the
  FTP SERVER cell shows; do not scan the network without need.
- The protocol and the server are documented in `docs/debug-tools.md` ("FTP server") and
  `platform/3ds/source/ftp_server_3ds.h`.

Host tests: `platform/3ds/tests/*_test.c` are plain C programs (see each file's
header for its sources); the updater's are run by
`platform/3ds/tests/run-update-tests.py`. Test on hardware whenever performance is
involved; Azahar is fine for logic but says nothing about Old 3DS timings.
