# The Minish Cap 3DS

Nintendo 3DS dual-screen port of *The Legend of Zelda: The Minish Cap*, aimed at smooth
60 fps on both New 3DS and Old 3DS, a wide view that fills the top screen, and on-console
tools for testing and debugging.

It installs as its own application, **Minish Cap 3DS**, and keeps its ROM and saves in
`sdmc:/3ds/Minish Cap 3DS/`. Downloads are on this repository's
[releases](https://github.com/tinaut1986/zelda-tmc-3ds/releases).

No ROM or extracted Nintendo game assets are distributed with this project. You must provide
your own legally obtained Game Boy Advance ROM.

## Origin

The game is the open-source decompilation of the original, running on a native engine:

* [zeldaret/tmc](https://github.com/zeldaret/tmc): the decompilation of the game.
* [Project Picori](https://github.com/999sian/tmc): the native engine and port infrastructure.
* [samyost1/tmc-android](https://github.com/samyost1/tmc-android): the dual-screen Android port
  the second screen comes from.
* [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds): the Nintendo 3DS
  port this repository was forked from. It has its own version line (`v0.1.0` onwards) and has
  since diverged: its own application identity, renderer work, bottom screen and tools.

## Features

* Both USA and European ROMs, detected automatically, with the European gameplay backports
  ([docs/eu-backport.md](docs/eu-backport.md)); no patched ROM is needed.
* Original, Stretch and Wide display modes, and **Full View**: Wide + Pixel Perfect draws every
  room at 400x240 (small rooms centred with black borders), on New and Old 3DS. The title
  screen, file select and the Minish portal cutscene are 400x240 too.
* Three display styles: Blur, Bilinear and Pixel Perfect. Everything is drawn by the GPU
  renderer; the software renderer remains as reference and fallback.
* New 3DS: 804 MHz mode, L2 cache, multi-core rendering, optional 2x-5x turbo on the C-Stick.
  Old 3DS: an optimised renderer, a lighter audio path and an adaptive frame pacer.
* X and Y fire items, shown as the console's A/B/X/Y diamond in the HUD and on the bottom
  screen.
* Bottom screen with live map, dungeon information, quest status, item touch controls, themes
  and settings; its own text follows the language chosen in-game (EN/FR/DE/ES/IT).
* **Save states** (STATES tab), independent of the build.
* **RetroAchievements** (trophy tab, softcore): see
  [docs/retroachievements.md](docs/retroachievements.md).
* In-app updates with a STABLE and a + BETAS channel.
* Stereoscopic 3D on the top screen: the HUD and text boxes come out of the screen; lower the
  console's 3D slider for the flat picture ([docs/stereo-3d.md](docs/stereo-3d.md)).
* Debug builds (the Beta releases) add a DEBUG tab: SD log, frame-time recorder, GPU CHECK,
  screen dump and an FTP server. See [docs/debug-tools.md](docs/debug-tools.md).

## Screenshots

_Original and extended views, side by side: coming soon._

<!-- Put the images or GIFs in docs/images/ and link them here. -->

## Widescreen and generative AI

The GBA drew some screens as one fixed 240x160 picture, so the columns beyond it do not exist
in the game. For Full View (Wide + Pixel Perfect) to fill the 400x240 screen, the missing
borders of a few scenes have been **extended with generative AI**, from a screen dump of the
original picture:

* the Minish portal cutscene,
* the forest of the title screen,
* the cave of the file select screen.

Only the generated parts are included (the original area is left transparent), and the game's
own picture is drawn untouched over them. The title's light rays are not AI: they are the
game's own rays, copied and continued by the port at run time. Everywhere else the extra
width is the game's real map. How it is done: [docs/wide-scene-extension.md](docs/wide-scene-extension.md).

## Installation

1. Install the CIA from the latest release with FBI (or scan the QR code in the release).
   There is no 3DSX release: from the Homebrew Launcher the game does not get enough memory.
2. Create this folder on your SD card:

```text
sdmc:/3ds/Minish Cap 3DS/
```

3. Place your legally obtained USA or European `.gba` ROM in it. Any filename works as long as
   the extension is `.gba`. The ROM stays on the SD card and is never included in the CIA.

If the folder has no ROM, the game reads the one in the original port's folder
(`sdmc:/3ds/The Minish Cap 3DS/`) and offers once to copy its saves. The two applications can
be installed side by side.

The **European** ROM is recommended: it is the one used the most during development and
testing, and it includes English. Expected clean ROM SHA-1 values:

```text
USA:    b4bd50e4131b027c334547b4524e2dbbd4227130
Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7
```

### Audio

Audio requires a working Nintendo 3DS DSP firmware setup. If homebrew audio is not working,
open the Luma3DS Rosalina Menu and use `Miscellaneous options > Dump DSP firmware`.

## Updates

Open **OPTIONS > UPDATES**, pick the **STABLE** or **+ BETAS** channel and install. Save your game
first: the application closes after a successful update. The CIA is checked against the
application's title ID and a SHA-256 digest before it replaces the installed one. Wi-Fi is
required; a failed connection leaves the current installation untouched.

## Diagnostics

Debug builds write numbered `debug/tmc-dump-NNNN-*` files (screenshots, memories, runtime
state, performance data) under:

```text
sdmc:/3ds/Minish Cap 3DS/debug/
```

Attach them to a bug report. [docs/debug-tools.md](docs/debug-tools.md) explains each one.

## Building

Requirements: devkitPro with devkitARM, libctru, Citro2D and Citro3D, and CMake. `makerom`
and `bannertool` are in `platform/3ds/tools/bin/`.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
./build_3ds.sh                          # interactive menu (debug build by default)
./build_3ds.sh --mode prod --no-ftp     # what a stable release is
./build_3ds.sh --install <3ds-ip>       # build, upload and install over the running game
```

The CIA is written to `build-3ds/game/tmc-3ds-v<VERSION>.cia` (a `.3dsx` is made alongside for
local use). The build does not include or embed a ROM. Contributor and workflow notes are in
[CLAUDE.md](CLAUDE.md) and [docs/PLAN.md](docs/PLAN.md); release notes in
[docs/release-notes](docs/release-notes).

## Credits

* [samyost1/tmc-android](https://github.com/samyost1/tmc-android): dual-screen Android source base
* [Project Picori](https://github.com/999sian/tmc): native Minish Cap engine and port infrastructure
* [Raekwon1603/tmc-android](https://github.com/Raekwon1603/tmc-android): Android packaging and platform work behind the dual-screen fork
* [zeldaret/tmc](https://github.com/zeldaret/tmc): original decompilation
* [EstebanPdN](https://github.com/EstebanPdN/zelda-tmc-3ds): the original Nintendo 3DS port this one is forked from
* [ipatix/agbplay](https://github.com/ipatix/agbplay): the m4a music engine
* [rcheevos](https://github.com/RetroAchievements/rcheevos): the RetroAchievements client

## License and Legal Notice

Original port contributions are distributed under **GNU GPL v3.0 or later**.
See [LICENSE](LICENSE) for the full terms and [COPYRIGHT.md](COPYRIGHT.md) for scope and attribution.

Each release's **Source code** download contains the corresponding application and library
sources and build scripts. The 3DS build uses **agbplay** by ipatix and contributors under
**LGPL v3.0**; its [notice](libs/agbplay_core/LICENSE) and
[license text](libs/agbplay_core/COPYING.LESSER) are included. You may modify that library and
rebuild the CIA with the documented build command.

Third-party components retain their respective licenses as listed in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

Nintendo owns *The Legend of Zelda*, *The Minish Cap*, and all associated game content.

This is an unofficial fan-made project and is not affiliated with or endorsed by Nintendo.

No ROM, extracted Nintendo game assets, save data, or firmware is distributed with this project.
