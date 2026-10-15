# RetroAchievements

The bottom screen's trophy tab (between STATES and OPTIONS) runs the Minish Cap sets of
[RetroAchievements](https://retroachievements.org) on the console, through
[rcheevos](https://github.com/RetroAchievements/rcheevos), as `../mzm` and `../sm-3ds` do.
Softcore only: RetroAchievements does not sanction unofficial ports, so there is no hardcore
mode, and save states do not stop the set.

## Using it

- **ACCOUNT**: LOG IN asks for the user name and password with the system keyboard. The
  password is sent once and never stored; the server's token is kept in
  `retroachievements.ini` in the data folder (it logs in as the player: keep the file to
  yourself). Once logged in the cell shows the user; two taps log out.
- **ACTIVE**: achievements on or off (logging in turns them on).
- **NOTICE**: where the unlock notice shows, TOP (over the game) or BOTTOM (over the tab bar,
  on any tab). **SOUND**: the unlock sound. **TEST**: a sample notice and sound.
- The line under them is the server's last message, or the connection state.
- The list: unlocked/total and points, the order (DEFAULT as rcheevos groups it, TITLE,
  POINTS, RECENT) and its direction (the arrow), four cards a page (`<` `>`). A card shows its
  badge (grey while locked), points, unlock date and RA's type mark (missable, progression,
  win condition); tapping it opens the badge at full size and the whole description.
- Each save state carries the achievements' progress: `states/tmc-state<id>.rap` next to the
  state, written when it is saved, read when it is loaded and deleted with it. A state saved
  before achievements were on resets their progress when loaded.

Files in the data folder: `retroachievements.ini` (settings, token, the ROM's hash),
`badges/` (the badge PNGs, downloaded once), `debug/retroachievements.log` (every server call
without its query string, logins, unlocks) and `debug/ra-set.json` (the last set downloaded:
the achievements' conditions, nothing of the account).

## Which ROM

RA's set for the game (id 559) lists only the **USA** ROM (md5 `a104896da0047abe8bee2a6e3f4c7290`,
No-Intro) and a few hacks of it. The port runs the USA and the European ROM, but with the European
one the server answers "Unsupported Game Version" (game `1200000559`, one achievement) and no set
loads: the port sends the file's true md5, so nothing is wrong on this side. The data variables
the set reads have the same addresses in both regions (`linker.ld`), so the mapping is expected to
work for both; asking RA to add the European hash is the owner's to do.

To try the set on the European ROM (testing, with a test account, never the real one): put the USA
md5 in `rom_md5=` of `retroachievements.ini`, leave `rom_key=` as it is (the port uses the cached
hash while the key matches the file) and relaunch. 2026-10-15: the set loads (66 achievements) and
unlocks work (the owner's check).

## How it works

`platform/3ds/source/retro_ach.c` is `../sm-3ds/source/retro_ach.c` adapted:

- **Threads.** Every `rc_client` call runs on the main thread: `RetroAch_DoFrame` after each
  game frame (`VBlankIntrWait`, `port/port_bios.c`), `RetroAch_Update` every loop iteration
  (`PumpLifecycleAndAudio`, `platform_3ds.c`), the save state hooks inside
  `SaveState_Service`. HTTP (libctru's `httpc`, `http:C` in `cia/tmc3ds.rsf`, falling back to
  plain HTTP when the console's TLS cannot reach the server), the ROM hash and the badges run on
  a low-priority worker; answers are handed back in `RetroAch_Update`. The bottom screen paints
  on its own thread and reads through copying accessors under a lock.
- **The game is identified** by the MD5 of the ROM file (RA's GBA hash), read again from the
  SD card on the worker the first time (the port patches its copy in memory), then cached in
  the ini with the file's path and size. The server picks the USA or EU set from it.
- **Memory.** RA addresses GBA memory as IWRAM at `0x00000` (GBA `0x03000000`) and EWRAM at
  `0x08000` (GBA `0x02000000`). The original game keeps its globals at fixed addresses there,
  which `linker.ld` lists; in this port they are ordinary C variables, so
  `tools/gen_ra_map.py` turns `linker.ld` plus the linked ELF into
  `platform/3ds/source/retro_ach_map_3ds.c`, a table of RA address -> the port's variable. The
  3DS is 32-bit ARM like the GBA, so the structs keep their layout (`SaveFile`,
  `RoomControls`, `Entity`/`GenericEntity`, `PlayerState` checked against the RA code notes
  of game 559). A byte no variable covers is read from `gIwram`/`gEwram`, where the port keeps
  what it still addresses the GBA way (the BG buffers).
- **The notice** on the top screen is painted on the CPU into a small texture when it changes
  and drawn as one quad after the frame (`DrawRaToast`, `platform_gpu_3ds.c`); on the bottom
  screen it is `Compact_PaintRaToast`. The sound (`romfs:/ra-unlock.pcm`, mono s16le 32 kHz,
  the same as mzm's and sm-3ds's) is mixed into the game's output buffer on the audio thread.
- rcheevos is built for size (`-Os`, as is `retro_ach.c`): the read-only segment has a pinned
  end for save states (`3dsx.ld`) and had 8 KB too little at `-O2`.

## When something does not unlock

1. `debug/retroachievements.log`: was the game loaded (`game loaded: id ...`), is the
   achievement already unlocked on the account (an unlocked one never triggers again)?
2. `debug/ra-set.json` has its conditions. Their addresses go through
   `retro_ach_map_3ds.c`: check that the variable there is the one the condition means, and
   peek it on the console (`tools/tmc-link/tmc-link.py peek <symbol>`).
3. Known limits: a condition that compares a **pointer** reads this build's native address,
   not the GBA one; a variable the port renamed or moved out of the decomp's layout (the
   generator prints the `linker.ld` names it could not find) reads `gIwram`/`gEwram`, i.e.
   usually zero. Neither has been seen in the set yet: it has not been run end to end.

Regenerate the map after `linker.ld` changes or a mapped variable is renamed (the link fails
otherwise): `python3 tools/gen_ra_map.py build-3ds/game/tmc-3ds.elf`.
