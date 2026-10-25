#include "scene_pictures_3ds.h"

#include "app_paths_3ds.h"
#include "cpu/mode1.h"
#include "fade_gpu_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern bool Port_Config_GetColorCorrection(void);
extern bool Port_Config_GetWideAi(void); /* these pictures are generated art: only WIDE + AI shows them */

#define TITLE_SD TMC3DS_APP_DIR "/title-forest.bin"
#define TITLE_ROMFS "romfs:/wide-scenes/title-forest.bin"
#define CAVE_SD TMC3DS_APP_DIR "/fileselect-cave.bin"
#define CAVE_ROMFS "romfs:/wide-scenes/fileselect-cave.bin"

static int sAvailable = -1; /* -1 not looked, 0 none, 1 a file */
static uint16_t* sFaded555;  /* scratch for the software renderer's faded copy */
static uint32_t* sFadedAbgr;
static uint16_t* sTitle555;
static uint32_t* sTitleAbgr;
static bool sAbgrCorrected;

bool ScenePictures_TitleAvailable(void) {
    if (!Port_Config_GetWideAi()) return false;
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
            sTitleAbgr[i] = gba[i] != 0 ? virtuappu_mode1_rgb555_to_abgr8888(gba[i]) : 0u;
        sAbgrCorrected = corrected;
    }
    return sTitleAbgr;
}

const uint32_t* ScenePictures_TitleAbgrFaded(void) {
    const uint16_t* gba = ScenePictures_Title555();
    if (gba == NULL) return NULL;
    const size_t count = SCENE_TITLE_W * SCENE_TITLE_H;
    if (sFaded555 == NULL) sFaded555 = malloc(count * sizeof(uint16_t));
    if (sFadedAbgr == NULL) sFadedAbgr = malloc(count * sizeof(uint32_t));
    if (sFaded555 == NULL || sFadedAbgr == NULL || !FadeGpu3DS_FadeShown(gba, sFaded555, count))
        return ScenePictures_TitleAbgr();
    for (size_t i = 0; i < count; ++i)
        sFadedAbgr[i] = sFaded555[i] != 0 ? virtuappu_mode1_rgb555_to_abgr8888(sFaded555[i]) : 0u;
    return sFadedAbgr;
}

void ScenePictures_Release(void) {
    free(sTitle555);
    free(sTitleAbgr);
    free(sFaded555);
    free(sFadedAbgr);
    sTitle555 = NULL;
    sTitleAbgr = NULL;
    sFaded555 = NULL;
    sFadedAbgr = NULL;
}

static int sCaveAvailable = -1; /* as sAvailable */
static uint8_t* sCave;

bool ScenePictures_CaveAvailable(void) {
    if (!Port_Config_GetWideAi()) return false;
    if (sCaveAvailable < 0) sCaveAvailable = access(CAVE_SD, F_OK) == 0 || access(CAVE_ROMFS, F_OK) == 0;
    return sCaveAvailable != 0;
}

const uint8_t* ScenePictures_CaveIndices(void) {
    if (sCave != NULL || !ScenePictures_CaveAvailable()) return sCave;
    FILE* f = fopen(CAVE_SD, "rb");
    if (f == NULL) f = fopen(CAVE_ROMFS, "rb");
    uint8_t* indices = malloc(SCENE_CAVE_W * SCENE_CAVE_H);
    if (f == NULL || indices == NULL || fread(indices, 1, SCENE_CAVE_W * SCENE_CAVE_H, f) != SCENE_CAVE_W * SCENE_CAVE_H) {
        if (f) fclose(f);
        free(indices);
        sCaveAvailable = 0;
        return NULL;
    }
    fclose(f);
    sCave = indices;
    return sCave;
}

void ScenePictures_ReleaseCave(void) {
    free(sCave);
    sCave = NULL;
}

static const char* const kShaftFiles[SCENE_SHAFT_LAYERS] = { "crenel-paths-ground.bin", "crenel-paths-ledges.bin",
                                                               "crenel-paths-shadows.bin" };
static const unsigned kShaftHeights[SCENE_SHAFT_LAYERS] = { SCENE_SHAFT_GROUND_H, SCENE_SHAFT_LAYER_H,
                                                            SCENE_SHAFT_LAYER_H };
static int8_t sShaftAvailable[SCENE_SHAFT_LAYERS] = { -1, -1, -1 }; /* as sAvailable */
static uint16_t* sShaft555[SCENE_SHAFT_LAYERS];
static int sShaftBg = -1, sShaftX, sShaftY[SCENE_SHAFT_LAYERS];

const uint16_t* ScenePictures_Shaft555(SceneShaftLayer layer, unsigned* height) {
    if (layer >= SCENE_SHAFT_LAYERS) return NULL;
    if (height) *height = kShaftHeights[layer];
    if (!Port_Config_GetWideAi()) return NULL; /* generated art: only WIDE + AI, checked every frame */
    if (sShaft555[layer] != NULL || sShaftAvailable[layer] == 0) return sShaft555[layer];
    char sd[160], romfs[96];
    snprintf(sd, sizeof(sd), "%s/%s", TMC3DS_APP_DIR, kShaftFiles[layer]);
    snprintf(romfs, sizeof(romfs), "romfs:/wide-scenes/%s", kShaftFiles[layer]);
    FILE* f = fopen(sd, "rb");
    if (f == NULL) f = fopen(romfs, "rb");
    const size_t count = (size_t)2 * SCENE_SHAFT_STRIP_W * kShaftHeights[layer];
    uint16_t* pixels = malloc(count * sizeof(uint16_t));
    if (f == NULL || pixels == NULL || fread(pixels, sizeof(uint16_t), count, f) != count) {
        if (f) fclose(f);
        free(pixels);
        sShaftAvailable[layer] = 0;
        return NULL;
    }
    fclose(f);
    for (size_t i = 0; i < count; ++i) { /* RGB565 to the GBA's 15-bit colour, as the title's */
        const uint16_t c = pixels[i];
        pixels[i] = (uint16_t)(((c >> 11) & 31u) | ((((c >> 5) & 63u) >> 1) << 5) | ((c & 31u) << 10));
    }
    sShaftAvailable[layer] = 1;
    sShaft555[layer] = pixels;
    return pixels;
}

void ScenePictures_ReleaseShaft(void) {
    for (unsigned i = 0; i < SCENE_SHAFT_LAYERS; ++i) {
        free(sShaft555[i]);
        sShaft555[i] = NULL;
    }
}

void ScenePictures_SetShaftView(int groundBg, int x, int groundY, int ledgesY, int shadowsY) {
    sShaftBg = groundBg;
    sShaftX = x;
    sShaftY[SCENE_SHAFT_GROUND] = groundY;
    sShaftY[SCENE_SHAFT_LEDGES] = ledgesY;
    sShaftY[SCENE_SHAFT_SHADOWS] = shadowsY;
}

int ScenePictures_ShaftView(int* x, int y[SCENE_SHAFT_LAYERS]) {
    if (x) *x = sShaftX;
    if (y)
        for (unsigned i = 0; i < SCENE_SHAFT_LAYERS; ++i) y[i] = sShaftY[i];
    return sShaftBg;
}
