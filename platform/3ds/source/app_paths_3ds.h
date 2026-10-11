#pragma once

/* Where this port keeps the ROM, saves, config, dumps and update downloads.
 * It is not the upstream port's "The Minish Cap 3DS" folder: this fork installs
 * as its own application (its own title ID, cia/tmc3ds.rsf) and must never read
 * or rewrite the other one's config or saves. */
#define TMC3DS_APP_DIR_FS "/3ds/Minish Cap 3DS"
#define TMC3DS_APP_DIR "sdmc:" TMC3DS_APP_DIR_FS

/* The CIA's title ID: UniqueId 0x5A22E in cia/tmc3ds.rsf (upstream is 0x5A22D).
 * The updater refuses a downloaded CIA that would install anything else. */
#define TMC3DS_TITLE_ID 0x0004000005a22e00ull
