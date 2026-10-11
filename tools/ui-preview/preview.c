/* Renders the bottom screen (port/port_second_screen.c) to images on the host.
 * Usage: preview OUTDIR "name:x,y;tN;..." ...
 * Each argument is one shot: starting from the MAP tab, the taps are replayed
 * (each followed by a paint, as on the console, where a tap acts on the
 * targets of the last painted frame), then the frame is written as
 * OUTDIR/name.ppm at 320x240. build.sh turns them into PNGs. "tN" taps the
 * Nth tab of the compact tab bar (MAP, ITEMS, QUEST, OPTIONS, DEBUG), wherever
 * this build puts it. */
#include "port_second_screen.h"
#include "port_second_screen_state.h"

#include "tmc3ds_build_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 320, H = 240 };
static uint32_t sPixels[W * H];

static void FakeSnapshot(SecondScreenSnapshot* s) {
    memset(s, 0, sizeof(*s));
    s->inGame = 1;
    s->hasWorldMap = 1;
    s->area = 0x02;
    s->room = 0x00;
    s->health = 8 * 9 + 4;
    s->maxHealth = 8 * 12;
    s->rupees = 287;
    s->walletType = 1;
    s->walletMax = 300;
    s->bombCount = 18;
    s->bombMax = 30;
    s->arrowCount = 25;
    s->arrowMax = 50;
    s->kinstoneFused = 23;
    s->kinstoneBag = 9;
    s->heartPieces = 2;
    s->figurineCount = 41;
    s->elements = 0x3;
    s->areaFlags = 0x01;
    s->playerX = 0x200;
    s->playerY = 0x300;
    s->equippedA = 0x01;
    s->equippedB = 0x07;
    s->equippedSlotA = 0xFF;
    s->equippedSlotB = 0xFF;
}

static void Paint(const SecondScreenSnapshot* snap, uint32_t tick) {
    memset(sPixels, 0, sizeof(sPixels));
    Port_SecondScreen_PaintInto(sPixels, W, H, W, snap, tick);
}

static void WritePpm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint32_t p = sPixels[i]; /* RGBA_8888 as A<<24 | B<<16 | G<<8 | R */
        unsigned char rgb[3] = { (unsigned char)p, (unsigned char)(p >> 8), (unsigned char)(p >> 16) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s OUTDIR name:x,y;x,y ...\n", argv[0]);
        return 2;
    }
    SecondScreenSnapshot snap;
    FakeSnapshot(&snap);
    uint32_t tick = 1;
    for (int a = 2; a < argc; a++) {
        char spec[512];
        snprintf(spec, sizeof(spec), "%s", argv[a]);
        char* taps = strchr(spec, ':');
        if (taps) *taps++ = 0;
        /* Back to a known state: tapping the active MAP tab keeps MAP, and
         * leaving through the settings tab twice returns from any page. */
        Port_SecondScreen_Init();
        Paint(&snap, tick++);
        for (char* t = taps ? strtok(taps, ";") : NULL; t; t = strtok(NULL, ";")) {
            int x, y, tab;
            if (sscanf(t, "t%d", &tab) == 1) {
                /* Same split as CColumn in bottom_compact_3ds.inc. */
                int n = TMC3DS_DEBUG_TOOLS ? 5 : 4, inner = W - 2 * 3 - (n - 1) * 3;
                x = (3 + inner * tab / n + tab * 3 + 3 + inner * (tab + 1) / n + tab * 3) / 2;
                y = 9;
                Port_SecondScreen_OnTap(x, y, 0);
                Paint(&snap, tick++);
            } else if (sscanf(t, "%d,%d", &x, &y) == 2) {
                Port_SecondScreen_OnTap(x, y, 0);
                Paint(&snap, tick++);
            }
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.ppm", argv[1], spec);
        WritePpm(path);
        printf("%s\n", path);
    }
    return 0;
}
