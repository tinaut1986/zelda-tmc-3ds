#include "debug_files_3ds.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int DebugFiles_Next(const char* kind, const char* suffix, char* path, unsigned cap) {
    if (mkdir(DEBUG_FILES_DIR, 0777) != 0 && errno != EEXIST) return -1;
    char last[96];
    snprintf(last, sizeof(last), DEBUG_FILES_DIR "/tmc-%s-last.txt", kind);
    int n = 0;
    FILE* f = fopen(last, "r");
    if (f) {
        if (fscanf(f, "%d", &n) != 1) n = 0;
        fclose(f);
    }
    n = n < 0 || n >= 9999 ? 1 : n + 1;
    f = fopen(last, "w");
    if (f) {
        fprintf(f, "%d\n", n);
        fclose(f);
    }
    snprintf(path, cap, DEBUG_FILES_DIR "/tmc-%s-%04d%s", kind, n, suffix);
    return n;
}
