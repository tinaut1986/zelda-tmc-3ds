/* Renders the bottom screen (port/port_second_screen.c) to images on the host.
 * Usage: preview OUTDIR "name:x,y;tN;..." ...
 * Each argument is one shot: starting from the MAP tab, the taps are replayed
 * (each followed by a paint, as on the console, where a tap acts on the
 * targets of the last painted frame), then the frame is written as
 * OUTDIR/name.ppm at 320x240. build.sh turns them into PNGs. "tN" taps the
 * Nth tab of the compact tab bar (MAP, ITEMS, QUEST, OPTIONS, DEBUG), wherever
 * this build puts it. THEME=n in the environment picks the compact UI theme
 * (bottom_compact_3ds.inc) and the panel backdrop that goes with it. */
#include "port_second_screen.h"
#include "port_second_screen_state.h"

#include "tmc3ds_build_config.h"

void Port_Config_SetUiTheme(int theme);
int Port_SecondScreen_3DS_UiTabCenterY(int height);
void Port_Config_SetSecondScreenBackdrop(int style);

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
    s->kinstoneBagOwned = 1;
    s->kinstoneFused = 12;
    s->swordSkills = 3;
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
    if (getenv("UILANG")) snap.language = atoi(getenv("UILANG")); /* 2 FR, 3 DE, 4 ES, 5 IT */
    const char* theme = getenv("THEME");
    if (theme) {
        /* As the theme button does: the theme and its panel backdrop
         * (PATTERN 0, CREAM 1, DARK 2, DIM 3, STONE 4, SLATE 5, NAVY 6). */
        static const int kBackdrop[] = { 5, 0, 6, 2 };
        int t = atoi(theme);
        Port_Config_SetUiTheme(t);
        if (t >= 0 && t < 4) Port_Config_SetSecondScreenBackdrop(kBackdrop[t]);
    } else {
        Port_Config_SetSecondScreenBackdrop(5);
    }
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
        /* Tabs keep their state between shots: start each from MAP (tapping
         * the active MAP tab keeps it; any other active tab goes there). */
        Port_SecondScreen_OnTap(4 + 15, Port_SecondScreen_3DS_UiTabCenterY(H), 0);
        Paint(&snap, tick++);
        for (char* t = taps ? strtok(taps, ";") : NULL; t; t = strtok(NULL, ";")) {
            int x, y, tab;
            if (sscanf(t, "t%d", &tab) == 1) {
                /* Same geometry as Compact_PaintTabBar: 30 px tabs every 34 px from x=4. */
                x = 4 + tab * 34 + 15;
                y = Port_SecondScreen_3DS_UiTabCenterY(H);
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
