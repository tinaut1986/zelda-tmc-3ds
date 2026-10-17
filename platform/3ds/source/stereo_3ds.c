#include "stereo_3ds.h"

#include "port_ppu_gpu_3ds_model.h"
#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#endif

#include <3ds.h>
#include <string.h>

extern bool Port_Config_Stereo3D(void);
extern int Port_Stereo_Scenes(void); /* PORT_STEREO_SCENE_* of port_widescreen.h */
enum { SCENE_TITLE_RAYS = 1, SCENE_LIGHT_RAYS = 2, SCENE_PATH_LEAVES = 4, SCENE_FILE_SELECT = 8, SCENE_LOGOS = 16 };

/* Pixels at the full slider, by layer (BG0..BG3, OBJ) and priority (docs/stereo-3d.md). In front of
 * the screen: BG0 at priority 0, the HUD and the text boxes, and the sprites at priority 0, the HUD's
 * buttons (the game draws Link and the world's sprites at 1-3). The rest on the screen plane. */
static const signed char kDepth[5][4] = {
    { 6, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 6, 0, 0, 0 },
};

/* Sun rays hang in front of the screen, behind the HUD's 6, only in the scenes that have them
 * (owner, 2026-10-17): the title's (BG0 at priority 2) and the Minish Woods' and the barrel
 * house's (BG3 at priority 0, LightRayManager). */
/* The leaves of the Minish Paths (BG1 at priority 1) hang the same way. */
enum { RAYS_DEPTH = 4, LEAVES_DEPTH = 4, TITLE_FRONT_DEPTH = 8, FILE_SELECT_OBJ_DEPTH = 4, FILE_SELECT_PANEL_DEPTH = 2 };

static bool sActive;
static bool sHasDepth;
static int sScenes;
static float sSlider;

void Stereo3DS_BeginFrame(void) {
    const bool setting = Port_Config_Stereo3D();
    sSlider = osGet3DSliderState();
    sActive = setting && sSlider > 0.0f;
#if TMC3DS_DEBUG_TOOLS
    static int lastSetting = -1, lastActive = -1;
    if (setting != lastSetting || (int)sActive != lastActive)
        DebugLog_Printf("stereo: setting %d, slider %.2f, %s", setting, (double)sSlider, sActive ? "3D on" : "2D");
    lastSetting = setting;
    lastActive = sActive;
#endif
    sScenes = sActive ? Port_Stereo_Scenes() : 0;
    sHasDepth = sScenes != 0;
#if TMC3DS_DEBUG_TOOLS
    static int lastScenes = -1;
    if (sScenes != lastScenes) DebugLog_Printf("stereo: scenes %d", sScenes);
    lastScenes = sScenes;
#endif
    if (sActive)
        for (unsigned layer = 0; layer < 5 && !sHasDepth; ++layer)
            for (unsigned priority = 0; priority < 4; ++priority)
                if (kDepth[layer][priority] != 0) sHasDepth = true;
}

bool Stereo3DS_Active(void) { return sActive; }

bool Stereo3DS_HasDepth(void) { return sHasDepth; }

float Stereo3DS_LayerShift(unsigned layer, unsigned priority, Stereo3DSEye eye) {
    if (!sHasDepth || layer > PPU_GPU3DS_OBJ || priority > 3) return 0.0f;
    int depth = kDepth[layer][priority];
    if (((sScenes & SCENE_TITLE_RAYS) && layer == PPU_GPU3DS_BG0 && priority == 2) ||
        ((sScenes & SCENE_LIGHT_RAYS) && layer == PPU_GPU3DS_BG3 && priority == 0))
        depth = RAYS_DEPTH;
    if ((sScenes & SCENE_PATH_LEAVES) && layer == PPU_GPU3DS_BG1 && priority == 1) depth = LEAVES_DEPTH;
    /* On the title the logo (sprites) and the sword (BG2 at priority 1) are drawn in front of the rays,
     * so they hang in front of them too (owner, 2026-10-17). */
    if ((sScenes & SCENE_TITLE_RAYS) && (layer == PPU_GPU3DS_OBJ || (layer == PPU_GPU3DS_BG2 && priority == 1)))
        depth = TITLE_FRONT_DEPTH;
    /* The Nintendo and Capcom logos are BG2 at priority 2 (owner, 2026-10-17). */
    if ((sScenes & SCENE_LOGOS) && layer == PPU_GPU3DS_BG2 && priority == 2) depth = FILE_SELECT_OBJ_DEPTH;
    /* The file select: its sprites (banner, slots, texts, the preview) hang in front of the cave, the preview's
     * panel (BG2) between them (owner, 2026-10-17). */
    if (sScenes & SCENE_FILE_SELECT) {
        if (layer == PPU_GPU3DS_OBJ) depth = FILE_SELECT_OBJ_DEPTH;
        else if (layer == PPU_GPU3DS_BG2 && priority == 2) depth = FILE_SELECT_PANEL_DEPTH;
        else if (layer == PPU_GPU3DS_BG1 && priority == 1) depth = FILE_SELECT_OBJ_DEPTH; /* hearts, languages, the name's letters */
    }
    const float half = 0.5f * (float)depth * sSlider;
    return eye == STEREO3DS_LEFT ? half : -half;
}
