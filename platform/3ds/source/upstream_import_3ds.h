#pragma once

#include <stdbool.h>

/* The upstream port's data folder (title 0x5A22D, "The Minish Cap 3DS"). This fork
 * never writes there; it only reads the ROM in place when its own folder has none,
 * and copies the saves once if the player agrees. */
#define UPSTREAM_APP_DIR "sdmc:/3ds/The Minish Cap 3DS"

/* True when the question is due: no save of our own, saves in UPSTREAM_APP_DIR, and
 * not asked before (the answer is kept in upstream-import.txt). Call with the working
 * directory in our data folder. */
bool UpstreamImport_ShouldOffer(void);

/* Ask on the bottom screen whether to copy those saves here (A) or not (B), and do it.
 * Nothing else may draw on the bottom screen meanwhile (stop the boot animation). */
void UpstreamImport_OfferSaves(void);
