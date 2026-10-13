/* Scene recorder (PLAN P1.6, docs/debug-tools.md "Scene recorder"), after ../sm-3ds: the top
 * screen as shown, full size, into a RAM ring as large as free memory allows; on stop the ring
 * goes to debug/tmc-scene-NNNN.bin and tools/scene-rec/decode.py turns it into PNGs and a GIF.
 * Debug builds only. Driven by SITE SCENEREC (tmc-link.py scenerec). */
#include "scene_rec_3ds.h"

#include "debug_files_3ds.h"
#include "debug_log_3ds.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool Platform3DS_CaptureTopRgb565(uint16_t* pixels);

enum {
    SCENE_W = 400,
    SCENE_H = 240,
    SCENE_PIXELS = SCENE_W * SCENE_H,
    SCENE_MAX_FRAMES = 1200,         /* 20 s at 60 Hz, 80 s at 15 Hz */
    SCENE_RESERVE = 8 * 1024 * 1024, /* left free for the game, its saves and the write */
};

/* One frame in the ring and in the file: the presented frame's number, then the pixels in the
 * framebuffer's order (400 columns of 240, each bottom to top), RGB565. */
typedef struct SceneFrame {
    uint32_t frame;
    uint16_t px[SCENE_PIXELS];
} SceneFrame;

static SceneFrame* sRing;
static int sCapacity, sHead, sCount, sEvery;
static uint32_t sTick;
static bool sWritePending;

bool SceneRec_Active(void) { return sRing != NULL && !sWritePending; }

/* As many frames as fit with SCENE_RESERVE still free. */
static bool Allocate(void) {
    for (int frames = SCENE_MAX_FRAMES; frames >= 8; frames = frames * 3 / 4) {
        SceneFrame* ring = malloc(sizeof(SceneFrame) * (size_t)frames);
        if (ring == NULL) continue;
        void* reserve = malloc(SCENE_RESERVE);
        if (reserve == NULL) {
            free(ring);
            continue;
        }
        free(reserve);
        sRing = ring;
        sCapacity = frames;
        return true;
    }
    return false;
}

bool SceneRec_Start(int every, char* reply, unsigned cap) {
    if (sRing != NULL) {
        snprintf(reply, cap, "450 Already recording (%d of %d frames).", sCount, sCapacity);
        return false;
    }
    if (!Allocate()) {
        snprintf(reply, cap, "550 No memory for a scene recording.");
        return false;
    }
    sEvery = every >= 1 && every <= 6 ? every : 2;
    sHead = sCount = 0;
    sTick = 0;
    sWritePending = false;
    snprintf(reply, cap, "200 Recording the top screen every %d frames: %d frames held (%.1f s at 60 fps).",
             sEvery, sCapacity, (double)sCapacity * sEvery / 60.0);
    DebugLog_Printf("scene rec: start, every %d frames, %d held", sEvery, sCapacity);
    return true;
}

bool SceneRec_Stop(char* reply, unsigned cap) {
    if (sRing == NULL || sWritePending) {
        snprintf(reply, cap, "450 Not recording.");
        return false;
    }
    sWritePending = true;
    snprintf(reply, cap, "202 Stopping: %d frames go to debug/tmc-scene-NNNN.bin at the next frame "
                         "(tmc-scene-last.txt has the number when it is written).", sCount);
    return true;
}

void SceneRec_Tick(uint32_t presentedFrame) {
    if (sRing == NULL) return;
    if (sWritePending) {
        /* The game stops while the ring is written: seconds for a long recording. */
        char path[64];
        const int number = DebugFiles_Next("scene", ".tmp", path, sizeof(path));
        FILE* f = number >= 0 ? fopen(path, "wb") : NULL;
        if (f != NULL) {
            const uint32_t head[5] = { 0x314E4353u /* SCN1 */, (uint32_t)sCount, SCENE_W, SCENE_H,
                                       (uint32_t)sEvery };
            fwrite(head, sizeof(head), 1, f);
            const int first = sCount < sCapacity ? 0 : sHead;
            for (int i = 0; i < sCount; ++i) fwrite(&sRing[(first + i) % sCapacity], sizeof(SceneFrame), 1, f);
            fclose(f);
            char final[64];
            snprintf(final, sizeof(final), "%.*s.bin", (int)(strlen(path) - 4), path);
            remove(final);
            rename(path, final);
            DebugLog_Printf("scene rec: %d frames -> %s", sCount, final);
        }
        free(sRing);
        sRing = NULL;
        sWritePending = false;
        return;
    }
    if (sTick++ % (uint32_t)sEvery != 0) return;
    SceneFrame* slot = &sRing[sHead];
    slot->frame = presentedFrame;
    if (!Platform3DS_CaptureTopRgb565(slot->px)) return;
    sHead = (sHead + 1) % sCapacity;
    if (sCount < sCapacity) ++sCount;
}
