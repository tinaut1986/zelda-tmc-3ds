#pragma once
/* Installs a CIA of this title from the SD card over the installed one, for
 * the debug FTP server's SITE INSTALL (ftp_server_3ds.c). The same steps as
 * the updater's install_cia (updater.c), which keeps its own copy because its
 * host test stubs them one by one. */
#include <stdbool.h>

/* `sdPath` is an SD path without "sdmc:" ("/3ds/..."). Refuses any CIA whose
 * title ID is not TMC3DS_TITLE_ID. On failure `error` says why. */
bool CiaInstall_File(const char* sdPath, char* error, unsigned errorCap);
