# Save states

PLAN P1.8 and P1.8b. In every build: the STATES tab on the bottom screen, and `SITE SAVESTATE` /
`LOADSTATE` / `DELSTATE` / `STATES` on the in-game FTP server in debug builds
(`tools/tmc-link/tmc-link.py savestate`, `loadstate N`, `states`).

Code: `platform/3ds/source/save_state_3ds.c` (files and jobs, the two formats),
`state_symbols_3ds.c` (the names of the game's globals and functions),
`state_pointers_3ds.c` (which words are pointers), `state_blob_3ds.c` (the reserved table),
`tools/gen_state_symbols.py` (fills it after the link), `tools/state-census/census.py` (checks
a state for pointers nobody covers), the STATES tab in `bottom_compact_3ds.inc`.

## Files

`states/tmc-state<id>.bin` in the data folder, with the top screen at the time in
`states/tmc-state<id>.thm` (200x120 RGB565). The list is open ended, newest first; the
number shown is the position in it (so it changes when a state is saved or deleted), the
id only names the file. A save is written beside the old file and swapped in, so a failed
one leaves the old state alone.

A state is a header (where, when, health, rupees, the build that saved it) and the game's
globals: everything the game keeps, including the emulated GBA memory (`gEwram`, `gVram`...),
the entities, the room, the player.

## Format 2: independent of the build

A state written by any build loads into any other build of the same ROM, whatever the game's
sources did in between (a global added, moved or removed, a function moved).

| Part | How |
|---|---|
| the game's `.data` / `.bss` objects (the brackets of `3dsx.ld`) | by **name**, with their size |
| pointers inside them | as relocations: another global's name + offset, or a function / constant of the game by the hash of its name + offset |
| pointers into the ROM image | moved on load: the image is allocated at boot, every word that points into the old place is moved |
| audio bookkeeping (`gMPlay*`) | not saved; the room music is restarted from its id |

The names come from the linked ELF: `tools/gen_state_symbols.py` runs after the link (CMake
POST_BUILD) and patches the table into `gStateSymbolBlob` (`state_blob_3ds.c`); the CIA is made
from that ELF. A binary that was not post-processed has no table and saves format 1.

On load: the file is read and parsed whole before anything is touched; each global is copied
back by name (a global this build no longer has is dropped, a new one keeps its live value, a
resized one gets the part both have); ROM pointers are moved; the relocations are applied; the
room music restarts.

Format 1 (a raw copy of `.data`/`.bss` with the game pinned at fixed addresses by
`3dsx.ld`: data at 0x620000, `.bss` at 0x630000) is still read, and loads only into a build
with the same layout fingerprint.

## What to keep in mind when changing the game's code

**Adding, removing, renaming or resizing a global is safe** (renaming loses its value in old
states).

**A new global that holds a pointer** needs to be covered, or a state keeps a stale address
after a rebuild and the game crashes on load. `state_pointers_3ds.c` has the rules:

1. A word that is exactly the start of a function or constant of the game, or of another saved
   global, is a pointer. Nothing to do.
2. A pointer into the *middle* of another global (an entity link, a table of pointers into
   `sTileSetsResolved`) is recognised only where the holder is known: add it to `kInner`. For
   a table of pointers into the middle of constants (`gBgAnimations`), `kConstInner`.
3. Globals bigger than 16 KB (tile maps, the emulated memories) are saved but never scanned.
4. `kNotSaved`: runtime objects and audio bookkeeping.

A pointer to something that is not the game's code, constants or globals (a heap object, a
libctru handle) cannot be saved: keep it out of the game's globals, or in `kNotSaved`.

**Check a change** with the census: save a state, fetch it, run it against the ELF of the
build that wrote it:

    tools/state-census/census.py tmc-state8.bin build-3ds/game/tmc-3ds.elf --uncovered

It lists words of the saved globals that point into the binary and that no relocation covers.
Most are numbers that look like addresses (positions, tile pairs, packed script data); one
that is really a pointer needs a rule. The real test is to save, change the game's code (any
global or function), rebuild, install and load: the position must be exact and the game must
go on.

## What to keep in mind

- **Audio**: the engine's own state is not part of a state; on load the saved room music is
  stopped and requested again.
- **Jobs** run at the top of the game's main loop (`SaveState_Service` in `AgbMain`), where
  no game function is half way through. Saving needs a game running; loading works from
  anywhere (title, file select, menus).
- **A new `port/` file with runtime objects** (threads, handles, `jmp_buf`s, pointers to heap
  buffers) must be left out of the game's objects in `3dsx.ld` (see the exclusions there), or
  those objects would be saved by name and restored dead.
- **The checkout's path** must not contain a folder named `port`, `libs` or `platform` above
  the repository, or the object patterns of `3dsx.ld` stop matching; the link then fails on the
  `.bss` bracket check.
- **A pin overflowing** fails the link with an `ASSERT` naming it; raise the pin. Only format 1
  states are lost by that.

## Not done

- Pointers to heap memory other than the ROM image, if the game side has any, are kept as
  they are; they are right while the boot allocations do not change.
- States of other scenes than the ones tried (the census was run on Hyrule Field): a holder
  nobody listed shows as a crash on load into a different build. Run the census on a state of
  each new kind of room.
- Format 1 states are not converted.
