#pragma once
/* The performance knobs of tmc3ds.ini, for the DEBUG tab's SWITCHES page
 * (implemented in port_config_3ds.c, next to the values they change). Every
 * one takes effect during play; each change is saved. */
#include <stdbool.h>

enum {
    DEBUG_SW_RENDERER,      /* gpu_renderer: GPU renderer, or the software PPU for comparison */
    DEBUG_SW_FRAME_SYNC,    /* gpu_frame_sync */
    DEBUG_SW_VBLANK_LOCK,   /* vblank_phase_lock */
    DEBUG_SW_STATIC_QUAD,   /* gpu_static_quad */
    DEBUG_SW_MAP_SKIP,      /* bottom_map_skip */
    DEBUG_SW_FRAME_LOG,     /* frame_log: upstream's per-line tmc3ds.log (slow) */
    DEBUG_SW_BOTTOM_CORE,   /* bottom_core: the painter thread is recreated before its next paint */
    DEBUG_SW_AUDIO_CORE,    /* audio_core: the refill thread is recreated at once */
    DEBUG_SW_COMPACT_UPLOAD,/* compact_upload (Old 3DS): taken at the start of the next frame */
    DEBUG_SW_CPU_LIMIT,     /* app_cpu_limit: APT takes it at once */
    DEBUG_SW_COUNT
};

const char* DebugSwitch_Name(int id);
void DebugSwitch_Value(int id, char* out, unsigned cap);
void DebugSwitch_Cycle(int id);
bool DebugSwitch_IsDefault(int id);
