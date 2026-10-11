#pragma once
/* The performance knobs of tmc3ds.ini, for the DEBUG tab's SWITCHES page
 * (implemented in port_config_3ds.c, next to the values they change). */
#include <stdbool.h>

enum {
    /* Read every frame: take effect at once. */
    DEBUG_SW_RENDERER,      /* gpu_renderer: GPU tile renderer or software PPU (Old 3DS) */
    DEBUG_SW_FRAME_SYNC,    /* gpu_frame_sync */
    DEBUG_SW_VBLANK_LOCK,   /* vblank_phase_lock */
    DEBUG_SW_STATIC_QUAD,   /* gpu_static_quad */
    DEBUG_SW_MAP_SKIP,      /* bottom_map_skip */
    DEBUG_SW_FRAME_LOG,     /* frame_log: upstream's per-line tmc3ds.log (slow) */
    /* Read at boot: shown as pending until the next launch. */
    DEBUG_SW_BOTTOM_CORE,   /* bottom_core */
    DEBUG_SW_AUDIO_CORE,    /* audio_core */
    DEBUG_SW_COMPACT_UPLOAD,/* compact_upload (Old 3DS) */
    DEBUG_SW_BOTTOM_565,    /* bottom_rgb565 */
    DEBUG_SW_CPU_LIMIT,     /* app_cpu_limit */
    DEBUG_SW_COUNT
};

void DebugSwitch_Init(void);
const char* DebugSwitch_Name(int id);
void DebugSwitch_Value(int id, char* out, unsigned cap);
void DebugSwitch_Cycle(int id);
bool DebugSwitch_AtBoot(int id);
/* Changed since boot, and only read at boot: not in effect yet. */
bool DebugSwitch_Pending(int id);
bool DebugSwitch_IsDefault(int id);
