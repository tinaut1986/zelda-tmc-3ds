#pragma once
/* A small FTP server inside the game, for debug-tools builds (DEBUG -> FTP), and an
 * install-only variant for production builds (dev-ftp.txt, below):
 * fetch logs, perf recordings and dumps, or drop files on the SD card, while
 * the game keeps running. No FBI or ftpd round trip.
 *
 * Anonymous (any user and password), one client at a time, passive mode only,
 * the whole SD card as "/" (install-only mode: /3ds/Minish Cap 3DS/update alone). Commands: USER PASS SYST FEAT TYPE MODE STRU PWD
 * CWD CDUP PASV EPSV LIST NLST MLSD RETR STOR APPE DELE MKD RMD RNFR RNTO SIZE
 * MDTM NOOP QUIT OPTS. Enough for FileZilla, WinSCP, curl and lftp.
 *
 * SITE INSTALL <path>: installs that CIA (it must be this game's title ID)
 * over the running game and relaunches into the new build. With curl:
 *   curl -T game.cia "ftp://IP:5000/3ds/Minish%20Cap%203DS/update/dev.cia" \
 *        -Q "-SITE INSTALL /3ds/Minish Cap 3DS/update/dev.cia"   (the "-": after the upload)
 * (./build_3ds.sh --install does both). The bottom screen says so meanwhile
 * (FtpServer_Notice, with FtpServer_Progress: percent and bar of the upload, of the install, then a
 * 10 s restart countdown; the upload's total is the size the client announced with ALLO before
 * STOR): an install never happens without a warning.
 *
 * Its thread runs at a lower priority than the game and blocks on the
 * network, so it costs nothing while idle. */
#include <stdbool.h>
#include <stdint.h>

enum { FTP_SERVER_PORT = 5000 };

bool FtpServer_Start(void);
/* Production builds: the install-only server, started when dev-ftp.txt is in the data folder. Only
 * /3ds/Minish Cap 3DS/update is reachable, the remote control (SITE but INSTALL) is off, and the
 * bottom screen shows the address with a CANCEL button (FtpServer_Cancel). Not started again
 * after a cancel: the player has to leave the game and open it again. */
bool FtpServer_StartInstallOnly(void);
bool FtpServer_InstallOnly(void); /* serving in that mode right now */
void FtpServer_Cancel(void);
void FtpServer_Stop(void);
/* From another thread (the audio worker's watchdog): logs to tmc3ds.log once when the server
 * thread has been inside one command for 30 s. */
void FtpServer_WatchdogPoll(void);
/* "waiting for a command for 3.2 s", "command PEEK for 41.0 s", or "off". */
void FtpServer_DescribeStage(char* out, unsigned cap);
bool FtpServer_Running(void);
/* "192.168.1.50:5000", or a short reason when it is not serving. */
void FtpServer_Address(char* out, unsigned cap);
/* Files sent and received since start, and whether a client is connected. */
void FtpServer_Stats(unsigned* sent, unsigned* received, bool* connected);
/* While a .cia is being received or SITE INSTALL runs, the line to show the
 * player (the bottom screen's toast); NULL otherwise. */
const char* FtpServer_Notice(void);
/* How far along that is: 1 while a .cia arrives, 2 while it installs, 3 during the wait before
 * the game restarts into it (10 s), 0 otherwise; `done` of `total` (bytes, or ms for 3). `total`
 * is 0 when the client did not announce the size with ALLO (no percent, no bar). Any thread. */
int FtpServer_Progress(unsigned long long* done, unsigned long long* total);
