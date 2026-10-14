#include "upstream_import_3ds.h"

#include <3ds.h>

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "port_text_i18n.h"
#include "boot_text_3ds.h"

#define MARKER_FILE "upstream-import.txt"
#define MAX_SAVES 16
#define NAME_SIZE 256

static bool HasSavExtension(const char* name) {
    const size_t length = strlen(name);
    if (length < 5) return false;
    const char* ext = name + length - 4;
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 's' && tolower((unsigned char)ext[2]) == 'a' &&
           tolower((unsigned char)ext[3]) == 'v';
}

/* The *.sav files of `dir` (regular files only), up to `max` names; returns the count. */
static int ListSaves(const char* dir, char names[][NAME_SIZE], int max) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL && count < max) {
        if (entry->d_name[0] == '.' || !HasSavExtension(entry->d_name)) continue;
        char path[NAME_SIZE * 2];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        snprintf(names[count++], NAME_SIZE, "%s", entry->d_name);
    }
    closedir(d);
    return count;
}

/* Copies through a temporary name, so an interrupted copy never leaves a short save
 * under the real one. An existing file of the same name is kept. */
static bool CopySave(const char* name) {
    struct stat info;
    if (stat(name, &info) == 0) return true;
    char from[NAME_SIZE * 2], temp[NAME_SIZE + 8];
    snprintf(from, sizeof(from), "%s/%s", UPSTREAM_APP_DIR, name);
    snprintf(temp, sizeof(temp), "%s.part", name);
    FILE* in = fopen(from, "rb");
    if (!in) return false;
    FILE* out = fopen(temp, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buffer[4096];
    bool ok = true;
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (ok && rename(temp, name) != 0) ok = false;
    if (!ok) remove(temp);
    return ok;
}

static void WriteMarker(const char* answer) {
    FILE* file = fopen(MARKER_FILE, "wb");
    if (!file) return;
    fprintf(file, "%s\n", answer);
    fclose(file);
}

static char sNames[MAX_SAVES][NAME_SIZE];

bool UpstreamImport_ShouldOffer(void) {
    struct stat info;
    if (stat(MARKER_FILE, &info) == 0) return false;
    if (ListSaves(".", sNames, MAX_SAVES) > 0) return false;
    return ListSaves(UPSTREAM_APP_DIR, sNames, MAX_SAVES) > 0;
}

void UpstreamImport_OfferSaves(void) {
    const int count = ListSaves(UPSTREAM_APP_DIR, sNames, MAX_SAVES);
    if (count == 0) return;

    const PortTextLanguage lang = BootText_Language();
    const uint16_t gold = BOOT_TEXT_GOLD, text = BOOT_TEXT_TEXT, faint = BOOT_TEXT_FAINT;
    const BootTextLine ask[] = {
        { Port_Text_TrFor(lang, "SAVES FROM THE MINISH CAP 3DS FOUND"), gold },
        { NULL, 0 },
        { Port_Text_TrFor(lang, "COPY THEM TO THIS APP?"), text },
        { Port_Text_TrFor(lang, "THE ORIGINAL APP KEEPS ITS OWN."), faint },
        { NULL, 0 },
        { Port_Text_TrFor(lang, "A: COPY"), text },
        { Port_Text_TrFor(lang, "B: SKIP"), text },
    };
    BootText_Show(ask, (int)(sizeof(ask) / sizeof(ask[0])));

    bool copy = false;
    for (;;) {
        if (!aptMainLoop()) return; /* closed from HOME: ask again next time */
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_A) {
            copy = true;
            break;
        }
        if (down & KEY_B) break;
        gspWaitForVBlank();
    }
    if (!copy) {
        WriteMarker("skipped");
        return;
    }

    bool ok = true;
    for (int i = 0; i < count; ++i) ok = CopySave(sNames[i]) && ok;
    WriteMarker(ok ? "copied" : "failed");
    const BootTextLine done[] = {
        { Port_Text_TrFor(lang, ok ? "SAVES COPIED" : "COPY FAILED"), ok ? gold : BOOT_TEXT_BAD },
    };
    BootText_Show(done, 1);
    svcSleepThread(1000000000LL);
}
