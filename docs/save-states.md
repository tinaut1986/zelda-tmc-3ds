# Save states

PLAN P1.8. In every build: the STATES tab on the bottom screen, and `SITE SAVESTATE` /
`LOADSTATE` / `DELSTATE` / `STATES` on the in-game FTP server in debug builds
(`tools/tmc-link/tmc-link.py savestate`, `loadstate N`, `states`).

Code: `platform/3ds/source/save_state_3ds.c` (files and jobs), `platform/3ds/3dsx.ld`
(the layout that makes it work), the STATES tab in `bottom_compact_3ds.inc`.

## Files

`states/tmc-state<id>.bin` in the data folder, with the top screen at the time in
`states/tmc-state<id>.thm` (200x120 RGB565). The list is open ended, newest first; the
number shown is the position in it (so it changes when a state is saved or deleted), the
id only names the file. A save is written beside the old file and swapped in, so a failed
one leaves the old state alone.

A state is a header (where, when, health, rupees, the build that saved it) and a raw copy
of the game's `.data` and `.bss`: everything the game keeps, including the emulated GBA
memory (`gEwram`, `gVram`...), the entities, the room, the player, the music players.

## Why a raw copy survives rebuilds

The game keeps pointers in its globals: to its own globals, its constant tables, its
functions and the ROM image. A raw copy only works if all of those are where they were.
`platform/3ds/3dsx.ld` (devkitARM's linker script with this added) puts the game's objects
(`src/` and the game-side files of `port/`) first in every section:

| What | Where |
|---|---|
| the game's code, then its constants | first in the code segment, which always starts at 0x100000 |
| the game's `.data` | first in the data segment, pinned at 0x5C0000 (the read-only segment is padded up to it) |
| the game's `.bss` | pinned at 0x5D0000, inside the data segment |

`KEEP` stops `--gc-sections` from keeping a different set of game functions when port code
starts or stops calling one. So a change to the port's own code (renderer, bottom screen,
platform, debug tools) moves nothing a state points to, and states from before it load.

Each state carries a fingerprint of the game's layout (the ends of those four blocks, the
`__ss_*` symbols). A build that changed the game's sources moves at least one of them, and
refuses older states (`STATE OF ANOTHER BUILD`; the card shows the area in red) instead of
loading dangling pointers. The fingerprint is not taken over the game's code bytes: they
hold the addresses of port functions the game calls, which move with every port change.

The ROM image is allocated at boot; if it sits elsewhere when a state loads, the words in
the restored data that point into its old place are moved (linear memory, an address range
the game's numbers never reach).

## What to keep in mind

- **Port files that are game state** are listed in `3dsx.ld` by being *left out* of the
  exclusions: everything in `port/` except the renderer, the bottom screen, the mixer and a
  few with runtime handles (`port_save.c`: the real save file must not be rewound;
  `port_bios.c` and `port_draw.c`: `jmp_buf`s; `port_softslots.c`, `port_dump_state.c`).
  A new `port/` file with runtime objects (threads, handles, `jmp_buf`s, pointers to heap
  buffers) must be added to the exclusions.
- **A pin overflowing** fails the link with an `ASSERT` naming it; raise the pin. That one
  build cannot load older states.
- **The checkout's path** must not contain a folder named `port`, `libs` or `platform`
  above the repository, or the object patterns stop matching; the link then fails on the
  `.bss` bracket check.
- **Music**: the music players come back with the rest; on load the saved room music is
  stopped and requested again so the mixer and the game agree.
- **Jobs** run at the top of the game's main loop (`SaveState_Service` in `AgbMain`), where
  no game function is half way through. Saving needs a game running; loading works from
  anywhere (title, file select, menus).

## Not done

- Pointers to heap memory other than the ROM image, if the game side has any, are kept as
  they are; they are right while the boot allocations do not change.
- A state across a change to the game's sources (the mzm approach, globals by name with
  their pointers relocated: `../mzm/docs/3ds-save-states.md`).
