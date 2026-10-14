#include "stereo_3ds.h"

#include "port_ppu_gpu_3ds_model.h"
#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#endif

#include <3ds.h>
#include <string.h>

extern int Port_Config_Stereo3D(void);

/* Pixels at the full slider, by layer (BG0..BG3, OBJ) and priority (docs/stereo-3d.md). In front of
 * the screen: BG0 at priority 0, the HUD and the text boxes, and the sprites at priority 0, the HUD's
 * buttons (the game draws Link and the world's sprites at 1-3). The rest on the screen plane. */
static const signed char kDepth[5][4] = {
    { 6, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 6, 0, 0, 0 },
};
/* STEREO3DS_TEST: what lies in front of the playfield pops out, to see that both eyes work. */
static const signed char kTestDepth[5][4] = {
    { 6, 0, 0, 0 }, { 6, 0, 0, 0 }, { 6, 0, 0, 0 }, { 6, 0, 0, 0 }, { 4, 2, 2, 0 },
};

static bool sActive;
static bool sHasDepth;
static float sSlider;
static const signed char (*sDepth)[4] = kDepth;

void Stereo3DS_BeginFrame(void) {
    const int setting = Port_Config_Stereo3D();
    sSlider = osGet3DSliderState();
    sActive = setting != STEREO3DS_OFF && sSlider > 0.0f;
    sDepth = setting == STEREO3DS_TEST ? kTestDepth : kDepth;
#if TMC3DS_DEBUG_TOOLS
    static int lastSetting = -1, lastActive = -1;
    if (setting != lastSetting || (int)sActive != lastActive)
        DebugLog_Printf("stereo: setting %d, slider %.2f, %s", setting, (double)sSlider, sActive ? "3D on" : "2D");
    lastSetting = setting;
    lastActive = sActive;
#endif
    sHasDepth = false;
    if (sActive)
        for (unsigned layer = 0; layer < 5 && !sHasDepth; ++layer)
            for (unsigned priority = 0; priority < 4; ++priority)
                if (sDepth[layer][priority] != 0) sHasDepth = true;
}

bool Stereo3DS_Active(void) { return sActive; }

bool Stereo3DS_HasDepth(void) { return sHasDepth; }

float Stereo3DS_LayerShift(unsigned layer, unsigned priority, Stereo3DSEye eye) {
    if (!sHasDepth || layer > PPU_GPU3DS_OBJ || priority > 3) return 0.0f;
    const float half = 0.5f * (float)sDepth[layer][priority] * sSlider;
    return eye == STEREO3DS_LEFT ? half : -half;
}
