#pragma once
/* A small FTP server inside the game, for debug-tools builds (DEBUG -> FTP):
 * fetch logs, perf recordings and dumps, or drop files on the SD card, while
 * the game keeps running. No FBI or ftpd round trip.
 *
 * Anonymous (any user and password), one client at a time, passive mode only,
 * the whole SD card as "/". Commands: USER PASS SYST FEAT TYPE MODE STRU PWD
 * CWD CDUP PASV EPSV LIST NLST MLSD RETR STOR APPE DELE MKD RMD RNFR RNTO SIZE
 * MDTM NOOP QUIT OPTS. Enough for FileZilla, WinSCP, curl and lftp.
 *
 * Its thread runs at a lower priority than the game and blocks on the
 * network, so it costs nothing while idle. */
#include <stdbool.h>
#include <stdint.h>

enum { FTP_SERVER_PORT = 5000 };

bool FtpServer_Start(void);
void FtpServer_Stop(void);
bool FtpServer_Running(void);
/* "192.168.1.50:5000", or a short reason when it is not serving. */
void FtpServer_Address(char* out, unsigned cap);
/* Files sent and received since start, and whether a client is connected. */
void FtpServer_Stats(unsigned* sent, unsigned* received, bool* connected);
