#include "scene_pictures_3ds.h"

#include "app_paths_3ds.h"
#include "cpu/mode1.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern bool Port_Config_GetColorCorrection(void);

#define TITLE_SD TMC3DS_APP_DIR "/title-forest.bin"
#define TITLE_ROMFS "romfs:/wide-scenes/title-forest.bin"

static int sAvailable = -1; /* -1 not looked, 0 none, 1 a file */
static uint16_t* sTitle555;
static uint32_t* sTitleAbgr;
static bool sAbgrCorrected;

bool ScenePictures_TitleAvailable(void) {
    if (sAvailable < 0) sAvailable = access(TITLE_SD, F_OK) == 0 || access(TITLE_ROMFS, F_OK) == 0;
    return sAvailable != 0;
}

const uint16_t* ScenePictures_Title555(void) {
    if (sTitle555 != NULL || !ScenePictures_TitleAvailable()) return sTitle555;
    FILE* f = fopen(TITLE_SD, "rb");
    if (f == NULL) f = fopen(TITLE_ROMFS, "rb");
    uint16_t* pixels = malloc(SCENE_TITLE_W * SCENE_TITLE_H * sizeof(uint16_t));
    if (f == NULL || pixels == NULL ||
        fread(pixels, sizeof(uint16_t), SCENE_TITLE_W * SCENE_TITLE_H, f) != SCENE_TITLE_W * SCENE_TITLE_H) {
        if (f) fclose(f);
        free(pixels);
        sAvailable = 0;
        return NULL;
    }
    fclose(f);
    /* The file is RGB565 (tools/scene-extend/to_console.py); back to the GBA's 15-bit colour. */
    for (unsigned i = 0; i < SCENE_TITLE_W * SCENE_TITLE_H; ++i) {
        const uint16_t c = pixels[i];
        pixels[i] = (uint16_t)(((c >> 11) & 31u) | ((((c >> 5) & 63u) >> 1) << 5) | ((c & 31u) << 10));
    }
    sTitle555 = pixels;
    return sTitle555;
}

const uint32_t* ScenePictures_TitleAbgr(void) {
    const uint16_t* gba = ScenePictures_Title555();
    if (gba == NULL) return NULL;
    const bool corrected = Port_Config_GetColorCorrection();
    if (sTitleAbgr == NULL) {
        sTitleAbgr = malloc(SCENE_TITLE_W * SCENE_TITLE_H * sizeof(uint32_t));
        if (sTitleAbgr == NULL) return NULL;
        sAbgrCorrected = !corrected;
    }
    if (sAbgrCorrected != corrected) {
        for (unsigned i = 0; i < SCENE_TITLE_W * SCENE_TITLE_H; ++i)
            sTitleAbgr[i] = virtuappu_mode1_rgb555_to_abgr8888(gba[i]);
        sAbgrCorrected = corrected;
    }
    return sTitleAbgr;
}

void ScenePictures_Release(void) {
    free(sTitle555);
    free(sTitleAbgr);
    sTitle555 = NULL;
    sTitleAbgr = NULL;
}
