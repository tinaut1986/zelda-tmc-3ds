#pragma once
/* Numbered debug files in debug/ of the data folder (cwd is the data folder).
 * Like ../sm-3ds: each capture takes the number after the last one written,
 * which debug/tmc-<kind>-last.txt remembers, so nothing is ever overwritten
 * and file times are never needed (libctru's stat() leaves st_mtime at 0). */
#include <stdbool.h>

#define DEBUG_FILES_DIR "debug"

/* Builds debug/tmc-<kind>-NNNN<suffix> for the next number and records it as
 * the last one. Returns the number, or -1 when the folder cannot be made. */
int DebugFiles_Next(const char* kind, const char* suffix, char* path, unsigned cap);
