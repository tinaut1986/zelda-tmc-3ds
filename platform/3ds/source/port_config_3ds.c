#include "port_runtime_config.h"
#include "platform_3ds.h"
#include "config_values_3ds.h"
#include "stereo_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#endif

enum { CONFIG_TEXT_CAP = 3072 };

/* BottomWorkerMain renders the settings panel while the main thread handles
 * taps and persists changes. Keep every value shared by those two threads
 * atomic; SaveConfig still performs its file I/O on the main thread and never
 * holds a compositor lock while touching the SD card. */
static _Atomic bool sShowFps;
static _Atomic int sUiTheme; /* bottom_compact_3ds.inc kCcThemes index */
static _Atomic bool sUiTabsBottom; /* OPTIONS -> TABS */
/* OPTIONS -> FRAMES, as in ../mzm and ../sm-3ds: PORT_3DS_PACING_* */
static _Atomic int sFramePacing;
static _Atomic bool sFollow = true;
static _Atomic bool sCrests = true;
static _Atomic bool sFloorReturn = true;
/* OPTIONS -> TOP HUD: 0 SHOW, 1 HIDE, 2 AUTO (hidden while the bottom screen shows its sidebar). */
enum { TOP_HUD_SHOW = 0, TOP_HUD_HIDE, TOP_HUD_AUTO, TOP_HUD_COUNT };
static _Atomic int sTopHud;
extern int Port_SecondScreen_TopHudCovered(void);
static _Atomic bool sHoldText;
static _Atomic bool sColorCorrection;
static _Atomic int sGbaGrid; /* 0 OFF .. 3 HIGH */
static _Atomic int sGbaFrame; /* 0 NO, 1 GBA, 2 BLUR */
static _Atomic int sNeighbourRooms = 1; /* the rooms beside a small one in Full View: 0 NO, 1 YES */
static _Atomic bool sAutosave;
static bool sConsoleParity;
static _Atomic Port3DSAspectRatio sAspectRatio = CONFIG_3DS_DEFAULT_ASPECT;
/* WIDE + AI, after ../mzm: the scenes' extensions made with a generative AI (the portal's sides, the
 * title's forest, the file select's cave) are shown only when the player wants them. Kept apart from
 * the aspect so every "is it WIDE" check stays as it is. `asked`: the bottom screen's one-time question
 * about it was answered. */
static _Atomic bool sWideAi;
static _Atomic bool sWideAiAsked;
/* OPTIONS -> UPDATES -> AUTO UPDATE: look for a newer build when the game opens and ask whether to
 * install it (updater.h, "Updater_AutoPrompt"). On unless the player turns it off. */
static _Atomic bool sAutoUpdate = true;
static _Atomic Port3DSDisplayStyle sDisplayStyle = CONFIG_3DS_DEFAULT_DISPLAY;
/* The desktop file-select overlay is rendered on the gameplay screen and
 * has no useful 3DS interaction path. Keep the native second-screen UI
 * separate and leave this desktop-only overlay disabled. */
static bool sPortSettings = false;
static bool sVsync = true;
static _Atomic float sVolume = 1.0f;
static _Atomic int sBackdrop;
static _Atomic unsigned sTurboMultiplier = 5;
static bool sConfigLoaded;
static char sConfigPath[256] = "tmc3ds.ini";
/* Lets a console session A/B the PICA200 renderer against the software
 * rasterizer without a rebuild. */
static bool sGpuRenderer = true;
/* Whether the GPU frame waits for the previous one to retire before the
 * builder rewrites the command buffers.
 *
 * Waiting is the correct thing in principle -- the GPU reads those buffers
 * asynchronously -- but it is off by default because it coincided with a
 * console showing a black top screen, and a renderer that draws is worth more
 * than one that is theoretically race-free. `gpu_frame_sync=1` turns it back
 * on for testing. The proper fix is double-buffered geometry, which needs the
 * presentation measurement first; see docs/old3ds-pica200-parity-notes.md. */
static bool sGpuFrameSync = false;
/* Where the rendered frame sits in the 512x256 target. The viewport and the
 * scissor are offset so scanline 0 lands in target row 0, which is where the
 * presenter samples -- but hardware parity fails from exactly row 96 down
 * (256 - 160, the offset itself), while an emulator passes, so the two
 * disagree about this axis. `gpu_viewport_offset=0` draws at the bottom of
 * the target instead, which is the other reading of the same convention. */
static bool sGpuViewportOffset = true;
/* How a batch's scanline range becomes a scissor rectangle.
 *   0 = none      : no vertical clipping at all (tile rows overhang)
 *   1 = flipped   : y measured from the bottom of the target (default)
 *   2 = direct    : y measured from the top
 * Hardware draws only the first band with the default while an emulator draws
 * both, so the two disagree about this axis; the switch settles it. */
static int sGpuScissorMode = 1;
/* Window/blend masking uses the stencil buffer. Turning it off draws every
 * batch unconditionally: windows and blending come out wrong, but if the
 * missing band appears then the stencil is what was rejecting it. */
static bool sGpuStencil = true;
static bool sGpuFade = true;
/* -1 keeps the built-in choice; 0 or 1 pins the audio worker for an A/B. */
static int sAudioCore = -1;
/* -1 AUTO (core 1 on the Old 3DS, 0 on the New 3DS); 0 or 1 pins the bottom-screen painter. */
static int sBottomCore = -1;
/* Skip a MAP-tab bottom-screen repaint when the quantised animation signature
 * says the next tick draws the same picture. Defaults on: a dump measured 2230
 * paints at 11.679 ms with 0 skips, ~1.95 ms per presented frame against a
 * 1.55 ms/frame deficit. Turn it off to A/B, or if the bottom screen ever
 * freezes -- that is the failure mode if the signature misses an input. */
static bool sBottomMapSkip = true;
/* Phase-lock the presentation wait to the next VBlank instead of accepting an
 * already-pending one. Off by default: unproven, and the failure mode is a
 * heavy scene pinned to 30 FPS. See Platform3DS_WaitForVBlank. */
static bool sVblankPhaseLock = false;
/* Interpolation for the offloaded NDSP voices.
 *
 * The software mix leaves channel 0 at 16364 Hz and NDSP upsamples it to its
 * 32728 Hz output with NDSP_INTERP_LINEAR -- exactly 2x, so linear inserts the
 * arithmetic midpoint and gently low-passes the GBA's aliasing. Before the DSP
 * offload every voice went through that one channel, so the whole mix shared
 * that character. The offload gave its own channels NDSP_INTERP_NONE, which
 * keeps the aliasing, so the mix has run two different resampling characters
 * at once ever since -- offloaded voices brighter than the software ones.
 *
 * Default matches channel 0, which is the pre-offload sound. Set to 0 for
 * NDSP_INTERP_NONE, which is closer to MP2K's NEAREST pitching in isolation
 * but inconsistent with the rest of the mix. */
static bool sAudioDspInterpLinear = true;
/* Speaker shaping. Off by default: it deliberately departs from GBA output, so
 * it is the player's call, not the port's. The cutoff is a knob because the
 * right value depends on the actual driver and on how NDSP's biquad interprets
 * f0 relative to its output rate -- tune it by ear, not from a datasheet. */
static bool sSpeakerEq = false;
static float sSpeakerEqHz = 280.0f;
/* Compact Old 3DS upload surfaces (272x160 top, 320x240 bottom RGBA8 instead of
 * 512x256). Cuts the bottom clean-and-transfer from 491520 to 307200 bytes, and
 * that transfer is synchronous, so it shortens a recurring main-thread block on
 * GSP rather than just saving memory. Off by default: the painter stride and the
 * display-transfer dimensions must agree, and if they do not the bottom screen
 * is visibly garbage. Flip it, look at the screen, then keep or drop it. */
static bool sCompactUpload = false;
/* Cap on the app's core-1 share. 0 keeps the built-in preference order (80 first).
 * Lower values hand core 1 back to the sysmodules -- GSP above all, which retires
 * the queue C3D_FrameBegin waits on. See Platform3DS_Init. */
static int sAppCpuLimit = 0;
/* Report and research items that measurement says are neutral or harmful by
 * default. Present, switchable, and off unless asked for. */
static bool sGpuStaticQuad = false;
/* Stereoscopic 3D (stereo_3ds.h): always on, and only while the console's 3D slider is up. */
static bool sStereo3D = true;
/* Depth of the world while playing, in pixels at the full slider (stereo_3ds.c): the layer over Link (BG1) and the
 * sprites. 0 switches that part off; stereo_world_on=0 switches both off. */
static bool sStereoWorld = true;
static int sStereoWorldBg1 = 2;
static int sStereoWorldObj = 0;
/* How much of its height over the ground a raised sprite (Link jumping, a flying enemy) stands out, in percent:
 * 25 puts a sprite 16 px up 4 px out of the screen. 0 switches it off. */
static int sStereoWorldHeight = 25;
/* How much of a sprite piece's height over the sprite's feet it stands out, in percent (25: a piece 16 px up, 4 px out). The lowest
 * piece (the feet, the shadow) stays on the ground's plane, so Link and the characters lean out instead of floating. */
static int sStereoWorldBody = 8;
static bool sBottomRgb565 = false;
static bool sGpuShortVertices = false;
/* Stored research choices. E10 disables both routes on every console because
 * they start voices immediately while channel 0 plays a queued software mix.
 * That phase mismatch separates a song's layers and also lets offloaded voices
 * continue through a quick-dump pause. */
static bool sAudioDsp = false;
static bool sAudioDspPcm = false;
/* E9 migrated Old 3DS installations to both unsafe offloads. Version 2
 * reverses that migration once so an existing tmc3ds.ini cannot preserve the
 * broken split-timeline mix. */
static int sPerformanceProfileVersion;
#if TMC3DS_DEBUG_TOOLS
/* debug_switches_3ds.h: the DEBUG tab tools restored at launch. */
static bool sDebugLogOn = true;
static bool sDebugLogDirect;
static bool sDebugFtpOn;
#endif

enum { PERFORMANCE_PROFILE_VERSION = 2 };

static bool ParseBool(const char* value) {
    return value != NULL && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                             value[0] == 'y' || value[0] == 'Y');
}

/* Every persisted setting as "key=value\n" lines, the body of tmc3ds.ini. */
static size_t FormatConfig(char* out, size_t cap) {
    size_t used = 0;
#define APPEND(...)                                                                     \
    do {                                                                                \
        int n_ = snprintf(out + used, cap - used, __VA_ARGS__);                        \
        if (n_ > 0) used = used + (size_t)n_ < cap ? used + (size_t)n_ : cap - 1;      \
    } while (0)
    APPEND("performance_profile_version=%d\n", PERFORMANCE_PROFILE_VERSION);
    APPEND("show_fps=%u\n", sShowFps ? 1u : 0u);
    APPEND("ui_theme=%d\n", (int)sUiTheme);
    APPEND("ui_tabs_bottom=%u\n", sUiTabsBottom ? 1u : 0u);
    APPEND("frame_pacing=%d\n", (int)sFramePacing);
    APPEND("follow_cam=%u\n", sFollow ? 1u : 0u);
    APPEND("windcrest_pins=%u\n", sCrests ? 1u : 0u);
    APPEND("floor_auto_return=%u\n", sFloorReturn ? 1u : 0u);
    APPEND("hide_top_hud=%d\n", (int)sTopHud);
    APPEND("hold_to_advance_text=%u\n", sHoldText ? 1u : 0u);
    APPEND("color_correction=%u\n", sColorCorrection ? 1u : 0u);
    APPEND("gba_grid=%d\n", (int)sGbaGrid);
    APPEND("gba_frame=%d\n", (int)sGbaFrame);
    APPEND("neighbour_rooms=%d\n", (int)sNeighbourRooms);
    APPEND("autosave=%u\n", sAutosave ? 1u : 0u);
    APPEND("widescreen=%u\n", sAspectRatio == PORT_3DS_ASPECT_WIDE ? 1u : 0u);
    APPEND("screen_aspect=%s\n", ConfigValues3DS_AspectName(sAspectRatio));
    APPEND("wide_ai=%u\n", sWideAi ? 1u : 0u);
    APPEND("wide_ai_asked=%u\n", sWideAiAsked ? 1u : 0u);
    APPEND("auto_update=%u\n", sAutoUpdate ? 1u : 0u);
    APPEND("display_style=%s\n", ConfigValues3DS_DisplayName(sDisplayStyle));
    APPEND("master_volume=%.2f\n", (double)sVolume);
    APPEND("panel_backdrop=%d\n", sBackdrop);
    APPEND("turbo_multiplier=%u\n", sTurboMultiplier);

    /* Round-trip the performance/research switches. SaveConfig parses these
     * but used to omit them, so any Settings change -- or any exit that saves
     * -- silently erased every hand-set flag from tmc3ds.ini and the next run
     * quietly reverted to compiled defaults. That destroyed the config of the
     * very run being measured; `dump-20260820-205902` was diagnosed only by
     * reading the offload counters back out of info.txt. Whatever is parsed
     * must be written. */
    APPEND("gpu_renderer=%u\n", sGpuRenderer ? 1u : 0u);
    APPEND("gpu_frame_sync=%u\n", sGpuFrameSync ? 1u : 0u);
    APPEND("gpu_viewport_offset=%u\n", sGpuViewportOffset ? 1u : 0u);
    APPEND("gpu_scissor_mode=%d\n", sGpuScissorMode);
    APPEND("gpu_stencil=%u\n", sGpuStencil ? 1u : 0u);
    APPEND("gpu_fade=%u\n", sGpuFade ? 1u : 0u);
    APPEND("audio_core=%d\n", sAudioCore);
    APPEND("bottom_core=%d\n", sBottomCore);
    APPEND("bottom_map_skip=%u\n", sBottomMapSkip ? 1u : 0u);
    APPEND("vblank_phase_lock=%u\n", sVblankPhaseLock ? 1u : 0u);
    APPEND("audio_dsp_interp_linear=%u\n", sAudioDspInterpLinear ? 1u : 0u);
    APPEND("speaker_eq=%u\n", sSpeakerEq ? 1u : 0u);
    APPEND("speaker_eq_hz=%.1f\n", (double)sSpeakerEqHz);
    APPEND("compact_upload=%u\n", sCompactUpload ? 1u : 0u);
    APPEND("app_cpu_limit=%d\n", sAppCpuLimit);
    APPEND("gpu_static_quad=%u\n", sGpuStaticQuad ? 1u : 0u);
    APPEND("stereo_3d_on=%u\n", sStereo3D ? 1u : 0u);
    APPEND("stereo_world_on=%u\n", sStereoWorld ? 1u : 0u);
    APPEND("stereo_world_bg1=%d\n", sStereoWorldBg1);
    APPEND("stereo_world_obj=%d\n", sStereoWorldObj);
    APPEND("stereo_world_height=%d\n", sStereoWorldHeight);
    APPEND("stereo_world_body=%d\n", sStereoWorldBody);
    APPEND("bottom_rgb565=%u\n", sBottomRgb565 ? 1u : 0u);
    APPEND("gpu_short_vertices=%u\n", sGpuShortVertices ? 1u : 0u);
    APPEND("audio_dsp=%u\n", sAudioDsp ? 1u : 0u);
    APPEND("audio_dsp_pcm=%u\n", sAudioDspPcm ? 1u : 0u);
#if TMC3DS_DEBUG_TOOLS
    APPEND("debug_log=%u\n", sDebugLogOn ? 1u : 0u);
    APPEND("debug_log_direct=%u\n", sDebugLogDirect ? 1u : 0u);
    APPEND("debug_ftp=%u\n", sDebugFtpOn ? 1u : 0u);
#endif
#undef APPEND
    return used;
}

#if TMC3DS_DEBUG_TOOLS
/* The settings as last written to the SD log, to log only what changed. */
static char sLoggedConfig[CONFIG_TEXT_CAP];

/* Calls fn(key, old, new) for each line of `now` that differs from `before`
 * (old is NULL for a key `before` lacks). */
static void ForEachChange(const char* before, const char* now,
                          void (*fn)(const char* key, int keyLen, const char* oldValue, int oldLen,
                                     const char* newValue, int newLen)) {
    for (const char* line = now; *line;) {
        const char* eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);
        const char* eq = memchr(line, '=', (size_t)(eol - line));
        if (eq) {
            const int keyLen = (int)(eq - line);
            const char* oldValue = NULL;
            int oldLen = 0;
            for (const char* b = before; *b;) {
                const char* beol = strchr(b, '\n');
                if (!beol) beol = b + strlen(b);
                if (beol - b > keyLen && b[keyLen] == '=' && memcmp(b, line, (size_t)keyLen) == 0) {
                    oldValue = b + keyLen + 1;
                    oldLen = (int)(beol - oldValue);
                    break;
                }
                b = *beol ? beol + 1 : beol;
            }
            const int newLen = (int)(eol - eq - 1);
            if (!oldValue || oldLen != newLen || memcmp(oldValue, eq + 1, (size_t)newLen) != 0)
                fn(line, keyLen, oldValue, oldLen, eq + 1, newLen);
        }
        line = *eol ? eol + 1 : eol;
    }
}

static void LogChange(const char* key, int keyLen, const char* oldValue, int oldLen, const char* newValue,
                      int newLen) {
    if (oldValue)
        DebugLog_Printf("setting: %.*s %.*s -> %.*s", keyLen, key, oldLen, oldValue, newLen, newValue);
    else
        DebugLog_Printf("setting: %.*s = %.*s", keyLen, key, newLen, newValue);
}

void Port_Config_3DSLogSettings(void) {
    char text[CONFIG_TEXT_CAP];
    FormatConfig(text, sizeof(text));
    /* Several keys per line, under the log's 512-byte line limit. */
    char line[400];
    size_t used = 0;
    for (const char* p = text; *p;) {
        const char* eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        const size_t len = (size_t)(eol - p);
        if (used && used + len + 2 > sizeof(line)) {
            DebugLog_Printf("settings: %.*s", (int)used, line);
            used = 0;
        }
        if (len + 2 < sizeof(line)) {
            if (used) line[used++] = ' ';
            memcpy(line + used, p, len);
            used += len;
        }
        p = *eol ? eol + 1 : eol;
    }
    if (used) DebugLog_Printf("settings: %.*s", (int)used, line);
    memcpy(sLoggedConfig, text, sizeof(text));
}

static void LogConfigChanges(const char* text) {
    if (!DebugLog_Running()) return;
    ForEachChange(sLoggedConfig, text, LogChange);
    snprintf(sLoggedConfig, sizeof(sLoggedConfig), "%s", text);
}
#endif

static void SaveConfig(void) {
    if (!sConfigLoaded) return;

    char text[CONFIG_TEXT_CAP];
    const size_t textLen = FormatConfig(text, sizeof(text));
#if TMC3DS_DEBUG_TOOLS
    LogConfigChanges(text);
#endif

    char temp[sizeof(sConfigPath) + 8];
    char backup[sizeof(sConfigPath) + 8];
    snprintf(temp, sizeof(temp), "%s.tmp", sConfigPath);
    snprintf(backup, sizeof(backup), "%s.bak", sConfigPath);

    FILE* file = fopen(temp, "wb");
    if (!file) return;
    fprintf(file, "# Minish Cap 3DS runtime settings\n");
    fwrite(text, 1, textLen, file);

    /* A synchronous SD flush can stall the 3DS main thread for seconds on
     * every Settings change. The temporary file plus close/rename/backup
     * sequence still protects this recoverable preferences file. */
    bool ok = fflush(file) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        remove(temp);
        return;
    }

    remove(backup);
    bool hadCurrent = access(sConfigPath, F_OK) == 0;
    if (hadCurrent && rename(sConfigPath, backup) != 0) {
        remove(temp);
        return;
    }
    if (rename(temp, sConfigPath) != 0) {
        if (hadCurrent) rename(backup, sConfigPath);
        remove(temp);
        return;
    }
    remove(backup);
}

void Port_Config_Load(const char* path) {
    if (path != NULL && path[0] != '\0') snprintf(sConfigPath, sizeof(sConfigPath), "%s", path);

    bool hasScreenAspect = false;
    bool hasTabsBottom = false;
    bool migratePerformanceProfile = false;
    FILE* file = fopen(sConfigPath, "rb");
    if (file) {
        char line[160];
        while (fgets(line, sizeof(line), file) != NULL) {
            char key[64];
            char value[64];
            if (line[0] == '#' || sscanf(line, " %63[^=]=%63s", key, value) != 2) continue;
            if (strcmp(key, "performance_profile_version") == 0)
                sPerformanceProfileVersion = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "show_fps") == 0) sShowFps = ParseBool(value);
            else if (strcmp(key, "ui_theme") == 0) sUiTheme = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "ui_tabs_bottom") == 0) {
                sUiTabsBottom = ParseBool(value);
                hasTabsBottom = true;
            }
            else if (strcmp(key, "frame_pacing") == 0) sFramePacing = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "gpu_renderer") == 0) sGpuRenderer = ParseBool(value);
            else if (strcmp(key, "gpu_frame_sync") == 0) sGpuFrameSync = ParseBool(value);
            else if (strcmp(key, "gpu_viewport_offset") == 0) sGpuViewportOffset = ParseBool(value);
            else if (strcmp(key, "gpu_scissor_mode") == 0) sGpuScissorMode = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "gpu_stencil") == 0) sGpuStencil = ParseBool(value);
            else if (strcmp(key, "gpu_fade") == 0) sGpuFade = ParseBool(value);
            else if (strcmp(key, "audio_core") == 0) sAudioCore = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "bottom_core") == 0) sBottomCore = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "bottom_map_skip") == 0) sBottomMapSkip = ParseBool(value);
            else if (strcmp(key, "vblank_phase_lock") == 0) sVblankPhaseLock = ParseBool(value);
            else if (strcmp(key, "audio_dsp_interp_linear") == 0)
                sAudioDspInterpLinear = ParseBool(value);
            else if (strcmp(key, "speaker_eq") == 0) sSpeakerEq = ParseBool(value);
            else if (strcmp(key, "speaker_eq_hz") == 0) sSpeakerEqHz = strtof(value, NULL);
            else if (strcmp(key, "compact_upload") == 0) sCompactUpload = ParseBool(value);
            else if (strcmp(key, "app_cpu_limit") == 0) sAppCpuLimit = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "gpu_static_quad") == 0) sGpuStaticQuad = ParseBool(value);
            else if (strcmp(key, "stereo_3d_on") == 0) sStereo3D = ParseBool(value);
            else if (strcmp(key, "stereo_world_on") == 0) sStereoWorld = ParseBool(value);
            else if (strcmp(key, "stereo_world_bg1") == 0) sStereoWorldBg1 = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "stereo_world_obj") == 0) sStereoWorldObj = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "stereo_world_height") == 0) sStereoWorldHeight = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "stereo_world_body") == 0) sStereoWorldBody = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "bottom_rgb565") == 0) sBottomRgb565 = ParseBool(value);
            else if (strcmp(key, "gpu_short_vertices") == 0) sGpuShortVertices = ParseBool(value);
            else if (strcmp(key, "audio_dsp") == 0) sAudioDsp = ParseBool(value);
            else if (strcmp(key, "audio_dsp_pcm") == 0) sAudioDspPcm = ParseBool(value);
            else if (strcmp(key, "follow_cam") == 0) sFollow = ParseBool(value);
            else if (strcmp(key, "windcrest_pins") == 0) sCrests = ParseBool(value);
            else if (strcmp(key, "floor_auto_return") == 0) sFloorReturn = ParseBool(value);
            else if (strcmp(key, "hide_top_hud") == 0) {
                /* 0/1 as older builds wrote them (the flag), 2 AUTO; true/false/yes also read. */
                sTopHud = value[0] == '2' ? TOP_HUD_AUTO : (ParseBool(value) ? TOP_HUD_HIDE : TOP_HUD_SHOW);
            }
            else if (strcmp(key, "hold_to_advance_text") == 0) sHoldText = ParseBool(value);
            else if (strcmp(key, "color_correction") == 0) sColorCorrection = ParseBool(value);
            else if (strcmp(key, "gba_grid") == 0) {
                const int level = atoi(value);
                sGbaGrid = level >= 0 && level <= 3 ? level : 0;
            } else if (strcmp(key, "gba_frame") == 0) {
                /* 0 NO, 1 GBA, 2 BLUR; older files hold a yes/no word */
                const int mode = atoi(value);
                sGbaFrame = mode >= 1 && mode <= 2 ? mode : (ParseBool(value) ? 1 : 0);
            }
            else if (strcmp(key, "neighbour_rooms") == 0) {
                sNeighbourRooms = atoi(value) != 0 || ParseBool(value);
            }
            else if (strcmp(key, "wide_ai") == 0) sWideAi = ParseBool(value);
            else if (strcmp(key, "wide_ai_asked") == 0) sWideAiAsked = ParseBool(value);
            else if (strcmp(key, "auto_update") == 0) sAutoUpdate = ParseBool(value);
            else if (strcmp(key, "autosave") == 0) sAutosave = ParseBool(value);
            else if (strcmp(key, "widescreen") == 0 && !hasScreenAspect)
                sAspectRatio = ParseBool(value) ? PORT_3DS_ASPECT_WIDE : PORT_3DS_ASPECT_ORIGINAL;
            else if (strcmp(key, "screen_aspect") == 0) {
                sAspectRatio = ConfigValues3DS_ParseAspect(value);
                hasScreenAspect = true;
            } else if (strcmp(key, "display_style") == 0) {
                sDisplayStyle = ConfigValues3DS_ParseDisplay(value);
            }
            else if (strcmp(key, "master_volume") == 0) sVolume = strtof(value, NULL);
            else if (strcmp(key, "panel_backdrop") == 0) sBackdrop = (int)strtol(value, NULL, 10);
            else if (strcmp(key, "turbo_multiplier") == 0) sTurboMultiplier = (unsigned)strtoul(value, NULL, 10);
#if TMC3DS_DEBUG_TOOLS
            else if (strcmp(key, "debug_log") == 0) sDebugLogOn = ParseBool(value);
            else if (strcmp(key, "debug_log_direct") == 0) sDebugLogDirect = ParseBool(value);
            else if (strcmp(key, "debug_ftp") == 0) sDebugFtpOn = ParseBool(value);
#endif
        }
        fclose(file);
    }

    /* Restore one synchronized software-mix timeline on every model. Also
     * normalize a manually carried research value so the saved ini describes
     * the effective profile truthfully. */
    if (sPerformanceProfileVersion < PERFORMANCE_PROFILE_VERSION ||
        sAudioDsp || sAudioDspPcm) {
        sAudioDsp = false;
        sAudioDspPcm = false;
        sPerformanceProfileVersion = PERFORMANCE_PROFILE_VERSION;
        migratePerformanceProfile = true;
    }

    /* FOREST used to put the tab bar below; it is its own option now (TABS).
     * Keep it where a FOREST player had it. */
    if (!hasTabsBottom && sUiTheme == 3) sUiTabsBottom = true;

    if (sVolume < 0.0f) sVolume = 0.0f;
    if (sVolume > 1.0f) sVolume = 1.0f;
    if (sBackdrop < 0 || sBackdrop > 6) sBackdrop = 0;
    if (sTurboMultiplier < 2 || sTurboMultiplier > 5) sTurboMultiplier = 5;
    Platform3DS_SetTurboMultiplier(sTurboMultiplier);
    sConfigLoaded = true;
    if (migratePerformanceProfile) SaveConfig();
}
void Port_Config_SetActiveSaveProfile(const char* path) { (void)path; }
bool Port_Config_GpuRenderer(void) { return sGpuRenderer; }
bool Port_Config_GpuFrameSync(void) { return sGpuFrameSync; }
bool Port_Config_GpuViewportOffset(void) { return sGpuViewportOffset; }
int Port_Config_GpuScissorMode(void) { return sGpuScissorMode; }
bool Port_Config_GpuStencil(void) { return sGpuStencil; }
bool Port_Config_GpuFade(void) { return sGpuFade; }
int Port_Config_AudioCore(void) { return sAudioCore; }
int Port_Config_BottomCore(void) { return sBottomCore; }
bool Port_Config_BottomMapSkip(void) { return sBottomMapSkip; }
bool Port_Config_VblankPhaseLock(void) { return sVblankPhaseLock; }
bool Port_Config_AudioDspInterpLinear(void) { return sAudioDspInterpLinear; }
bool Port_Config_SpeakerEq(void) { return sSpeakerEq; }
float Port_Config_SpeakerEqHz(void) { return sSpeakerEqHz; }
bool Port_Config_CompactUpload(void) { return sCompactUpload; }
int Port_Config_AppCpuLimit(void) { return sAppCpuLimit; }
bool Port_Config_GpuStaticQuad(void) { return sGpuStaticQuad; }
bool Port_Config_Stereo3D(void) { return sStereo3D; }
int Port_Config_StereoWorldDepth(int which) {
    if (!sStereoWorld) return 0;
    const int depth = which == 0 ? sStereoWorldBg1 : sStereoWorldObj;
    return depth < -16 ? -16 : depth > 16 ? 16 : depth;
}
/* The depth of a sprite `height` px over the ground, in quarter pixels at the full slider (PpuGpu3DSBatch.depth). */
int Port_Config_StereoHeightDepthQ(int height) {
    if (!sStereo3D || !sStereoWorld || sStereoWorldHeight <= 0 || height <= 0 || !Stereo3DS_Active()) return 0;
    const int q = height * sStereoWorldHeight / 25;
    return q > 20 ? 20 : q;
}
/* The depth of a sprite piece whose bottom edge is `height` px over the sprite's feet, in quarter pixels at the full slider. */
int Port_Config_StereoBodyDepthQ(int height) {
    if (!sStereo3D || !sStereoWorld || sStereoWorldBody <= 0 || height <= 0 || !Stereo3DS_Active()) return 0;
    const int q = height * sStereoWorldBody / 25;
    return q > 12 ? 12 : q;
}
int Port_Config_GetStereoWorld(void) { return sStereoWorld ? 1 : 0; }
const char* Port_Config_GetStereoWorldName(void) { return sStereoWorld ? "ON" : "OFF"; }
void Port_Config_CycleStereoWorld(void) {
    sStereoWorld = !sStereoWorld;
    SaveConfig();
}
bool Port_Config_BottomRgb565(void) { return sBottomRgb565; }
bool Port_Config_GpuShortVertices(void) { return sGpuShortVertices; }
bool Port_Config_AudioDsp(void) {
    /* Immediate hardware voices cannot be synchronized with channel 0's
     * buffered timeline. Keep the parsed keys for diagnostics, but never let
     * an old hand-edited ini re-enable the broken mixed path. */
    return false;
}
bool Port_Config_AudioDspPcm(void) {
    return false;
}
/* ---- Performance switches for the DEBUG tab (debug_switches_3ds.h) ----
 * The ini-only knobs above, changeable on the console during play. Every
 * change is saved like any other setting. `bottom_rgb565` is left out: per
 * its own note above it turns the bottom screen red. */
#include "debug_switches_3ds.h"
extern void PortPpuGpu3DS_SetTintMode(int mode);
extern int PortPpuGpu3DS_TintMode(void);
extern void Stereo3DS_SetForce(bool on);
extern bool Stereo3DS_Forced(void);

#if TMC3DS_DEBUG_TOOLS
bool Port_Config_DebugLogOn(void) { return sDebugLogOn; }
void Port_Config_SetDebugLogOn(bool on) {
    if (sDebugLogOn == on) return;
    sDebugLogOn = on;
    SaveConfig();
}
bool Port_Config_DebugLogDirect(void) { return sDebugLogDirect; }
void Port_Config_SetDebugLogDirect(bool direct) {
    if (sDebugLogDirect == direct) return;
    sDebugLogDirect = direct;
    SaveConfig();
}
bool Port_Config_DebugFtpOn(void) { return sDebugFtpOn; }
void Port_Config_SetDebugFtpOn(bool on) {
    if (sDebugFtpOn == on) return;
    sDebugFtpOn = on;
    SaveConfig();
}
#endif

extern void Port_Audio_3DSRestartThread(void);

static int SwitchValue(int id) {
    switch (id) {
        case DEBUG_SW_RENDERER: return sGpuRenderer;
        case DEBUG_SW_FRAME_SYNC: return sGpuFrameSync;
        case DEBUG_SW_VBLANK_LOCK: return sVblankPhaseLock;
        case DEBUG_SW_STATIC_QUAD: return sGpuStaticQuad;
        case DEBUG_SW_MAP_SKIP: return sBottomMapSkip;
        case DEBUG_SW_BOTTOM_CORE: return sBottomCore;
        case DEBUG_SW_AUDIO_CORE: return sAudioCore;
        case DEBUG_SW_COMPACT_UPLOAD: return sCompactUpload;
        case DEBUG_SW_CPU_LIMIT: return sAppCpuLimit;
        case DEBUG_SW_STEREO_3D: return sStereo3D ? 1 : 0;
        case DEBUG_SW_PLANE_TINT: return PortPpuGpu3DS_TintMode();
        case DEBUG_SW_FORCE_3D: return Stereo3DS_Forced() ? 1 : 0;
        default: return 0;
    }
}

const char* DebugSwitch_Name(int id) {
    static const char* const kNames[DEBUG_SW_COUNT] = {
        "RENDERER", "FRAME SYNC", "VBLANK LOCK", "STATIC QUAD", "MAP SKIP",
        "BOTTOM CORE", "AUDIO CORE", "COMPACT UP", "CPU LIMIT", "STEREO 3D", "PLANE TINT", "FORCE 3D",
    };
    return id >= 0 && id < DEBUG_SW_COUNT ? kNames[id] : "?";
}

bool DebugSwitch_IsDefault(int id) {
    static const int kDefaults[DEBUG_SW_COUNT] = { 1, 0, 0, 0, 1, -1, -1, 0, 0, 0, 0, 0 };
    return id >= 0 && id < DEBUG_SW_COUNT && SwitchValue(id) == kDefaults[id];
}

void DebugSwitch_Value(int id, char* out, unsigned cap) {
    const int v = SwitchValue(id);
    switch (id) {
        case DEBUG_SW_RENDERER:
            snprintf(out, cap, "%s", v ? "GPU" : "CPU (TEST)");
            return;
        case DEBUG_SW_PLANE_TINT:
            snprintf(out, cap, "%s", v == 1 ? "LAYERS" : (v == 2 ? "DEPTH" : "OFF"));
            return;
        case DEBUG_SW_BOTTOM_CORE:
            snprintf(out, cap, "%s", v < 0 ? (Platform3DS_IsNew3DS() ? "AUTO 0" : "AUTO 1") : (v ? "CORE 1" : "CORE 0"));
            return;
        case DEBUG_SW_AUDIO_CORE:
            snprintf(out, cap, "%s", v < 0 ? "AUTO" : (v ? "CORE 1" : "CORE 0"));
            return;
        case DEBUG_SW_COMPACT_UPLOAD:
            snprintf(out, cap, "%s", Platform3DS_IsNew3DS() ? "OLD 3DS ONLY" : (v ? "ON" : "OFF"));
            return;
        case DEBUG_SW_CPU_LIMIT:
            if (v <= 0) snprintf(out, cap, "AUTO %u%%", Platform3DS_Core1TimeLimit());
            else snprintf(out, cap, "%d%% (%u%%)", v, Platform3DS_Core1TimeLimit());
            return;
        default:
            snprintf(out, cap, "%s", v ? "ON" : "OFF");
            return;
    }
}

void DebugSwitch_Cycle(int id) {
    switch (id) {
        case DEBUG_SW_RENDERER: sGpuRenderer = !sGpuRenderer; break;
        case DEBUG_SW_FRAME_SYNC: sGpuFrameSync = !sGpuFrameSync; break;
        case DEBUG_SW_VBLANK_LOCK: sVblankPhaseLock = !sVblankPhaseLock; break;
        case DEBUG_SW_STATIC_QUAD: sGpuStaticQuad = !sGpuStaticQuad; break;
        case DEBUG_SW_STEREO_3D: sStereo3D = !sStereo3D; break;
        case DEBUG_SW_PLANE_TINT: PortPpuGpu3DS_SetTintMode((PortPpuGpu3DS_TintMode() + 1) % 3); return; /* not saved */
        case DEBUG_SW_FORCE_3D: Stereo3DS_SetForce(!Stereo3DS_Forced()); return; /* not saved */
        case DEBUG_SW_MAP_SKIP: sBottomMapSkip = !sBottomMapSkip; break;
        case DEBUG_SW_BOTTOM_CORE:
            sBottomCore = sBottomCore < 0 ? 0 : (sBottomCore == 0 ? 1 : -1);
            Platform3DS_RestartBottomWorker();
            break;
        case DEBUG_SW_AUDIO_CORE:
            sAudioCore = sAudioCore < 0 ? 0 : (sAudioCore == 0 ? 1 : -1);
            Port_Audio_3DSRestartThread();
            break;
        case DEBUG_SW_COMPACT_UPLOAD:
            if (Platform3DS_IsNew3DS()) return;
            sCompactUpload = !sCompactUpload; /* Port_PPU_PresentFrame takes it */
            break;
        case DEBUG_SW_CPU_LIMIT: {
            static const int kSteps[] = { 0, 80, 70, 50, 30 };
            int next = 0;
            for (int i = 0; i < 5; i++)
                if (kSteps[i] == sAppCpuLimit) next = kSteps[(i + 1) % 5];
            sAppCpuLimit = next;
            Platform3DS_ApplyCpuLimit();
            break;
        }
        default: return;
    }
    SaveConfig();
}

u8 Port_Config_WindowScale(void) { return 1; }
const char* Port_Config_UpscaleMethod(void) { return "nearest"; }
u64 Port_Config_FrameTimeNs(void) { return 16666667ULL; }
u32 Port_Config_TargetFps(void) { return 60; }
u64 Port_Config_TickTimeNs(void) { return 16666667ULL; }
bool Port_Config_GetDecoupleRender(void) { return false; }
void Port_Config_SetDecoupleRender(bool on) { (void)on; }
bool Port_Config_GetShowFps(void) { return sShowFps; }
void Port_Config_SetShowFps(bool on) { sShowFps = on; SaveConfig(); }
int Port_Config_GetUiTheme(void) { return sUiTheme; }
void Port_Config_SetUiTheme(int theme) { sUiTheme = theme; SaveConfig(); }
bool Port_Config_GetUiTabsBottom(void) { return sUiTabsBottom; }
void Port_Config_SetUiTabsBottom(bool bottom) { sUiTabsBottom = bottom; SaveConfig(); }
int Port_Config_GetFramePacing(void) {
    int v = sFramePacing;
    return v >= 0 && v < PORT_3DS_PACING_COUNT ? v : PORT_3DS_PACING_AUTO;
}
void Port_Config_SetFramePacing(int pacing) { sFramePacing = pacing; SaveConfig(); }
bool Port_Config_GetTouchControls(void) { return false; }
void Port_Config_SetTouchControls(bool on) { (void)on; }
bool Port_Config_PortSettingsMenuEnabled(void) { return sPortSettings; }
void Port_Config_SetWindowScale(u8 scale) { (void)scale; }
void Port_Config_SetUpscaleMethod(const char* method) { (void)method; }
void Port_Config_SetTargetFps(u32 fps) { (void)fps; }
void Port_Config_CycleTargetFps(int direction) { (void)direction; }
u8 Port_Config_InternalScale(void) { return 1; }
void Port_Config_SetInternalScale(u8 scale) { (void)scale; }
void Port_Config_CycleInternalScale(int direction) { (void)direction; }
PortTouchScheme Port_Config_TouchScheme(void) { return PORT_TOUCH_SCHEME_DPAD; }
void Port_Config_SetTouchScheme(PortTouchScheme scheme) { (void)scheme; }
void Port_Config_CycleTouchScheme(int direction) { (void)direction; }
float Port_Config_TouchScale(void) { return 1.0f; }
void Port_Config_SetTouchScale(float scale) { (void)scale; }
float Port_Config_TouchOpacity(void) { return 1.0f; }
void Port_Config_SetTouchOpacity(float opacity) { (void)opacity; }
bool Port_Config_WidescreenEnabled(void) { return sAspectRatio == PORT_3DS_ASPECT_WIDE; }
void Port_Config_SetWidescreenEnabled(bool enabled) {
    sAspectRatio = enabled ? PORT_3DS_ASPECT_WIDE : PORT_3DS_ASPECT_ORIGINAL;
    SaveConfig();
}
void Port_Config_ToggleWidescreen(void) { Port_Config_SetWidescreenEnabled(!Port_Config_WidescreenEnabled()); }
bool Port_Config_GetConsoleParity(void) { return sConsoleParity; }
void Port_Config_SetConsoleParity(bool on) { sConsoleParity = on; }
void Port_Config_ToggleConsoleParity(void) { sConsoleParity = !sConsoleParity; }
PortAspectMode Port_Config_AspectMode(void) { return PORT_ASPECT_NATIVE_3_2; }
const char* Port_Config_AspectModeName(PortAspectMode mode) { (void)mode; return "Native"; }
void Port_Config_SetAspectMode(PortAspectMode mode) { (void)mode; }
void Port_Config_CycleAspectMode(int direction) { (void)direction; }
PortBgFill Port_Config_BgFill(void) { return PORT_BG_FILL_BLACK; }
const char* Port_Config_BgFillName(PortBgFill fill) { (void)fill; return "Black"; }
void Port_Config_SetBgFill(PortBgFill fill) { (void)fill; }
void Port_Config_CycleBgFill(int direction) { (void)direction; }
void Port_Config_BgFillColor(u8* r, u8* g, u8* b) { *r = *g = *b = 0; }
void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b) { (void)r; (void)g; (void)b; }
PortRenderBackend Port_Config_RenderBackend(void) { return PORT_RENDER_BACKEND_SOFTWARE; }
const char* Port_Config_RenderBackendName(PortRenderBackend b) { (void)b; return "3DS CPU"; }
void Port_Config_SetRenderBackend(PortRenderBackend b) { (void)b; }
void Port_Config_CycleRenderBackend(int direction) { (void)direction; }

#define BOOL_CONFIG(name, initial) \
    bool Port_Config_Get##name(void) { return initial; } \
    void Port_Config_Set##name(bool on) { (void)on; }
BOOL_CONFIG(TtsEnabled, false)
BOOL_CONFIG(A11yCues, false)
BOOL_CONFIG(A11yFootsteps, false)
BOOL_CONFIG(A11yHazards, false)
BOOL_CONFIG(A11yRadar, false)
BOOL_CONFIG(A11yWalls, false)
BOOL_CONFIG(PracticeShowTimer, false)
BOOL_CONFIG(PracticeShowInputs, false)
BOOL_CONFIG(PracticeShowHistory, false)
BOOL_CONFIG(DiscordRpc, false)
BOOL_CONFIG(GpuRaster, false)
BOOL_CONFIG(GpuRasterGles, false)
BOOL_CONFIG(LcdPersistence, false)
BOOL_CONFIG(RibbonEnabled, false)
BOOL_CONFIG(MenuHintSeen, true)
BOOL_CONFIG(RollAttackMacroEnabled, false)
BOOL_CONFIG(Fullscreen, true)
BOOL_CONFIG(FullscreenHideCursor, false)

float Port_Config_GetTtsRate(void) { return 1.0f; }
void Port_Config_SetTtsRate(float v) { (void)v; }
float Port_Config_GetTtsPitch(void) { return 1.0f; }
void Port_Config_SetTtsPitch(float v) { (void)v; }
float Port_Config_GetTtsVolume(void) { return 1.0f; }
void Port_Config_SetTtsVolume(float v) { (void)v; }
const char* Port_Config_GetTtsVoice(void) { return ""; }
void Port_Config_SetTtsVoice(const char* v) { (void)v; }
const char* Port_Config_GetTtsLanguage(void) { return "en"; }
void Port_Config_SetTtsLanguage(const char* v) { (void)v; }
bool Port_Config_GetSecondScreenFollowCam(void) { return sFollow; }
void Port_Config_SetSecondScreenFollowCam(bool on) { sFollow = on; SaveConfig(); }
bool Port_Config_GetSecondScreenCrestPins(void) { return sCrests; }
void Port_Config_SetSecondScreenCrestPins(bool on) { sCrests = on; SaveConfig(); }
bool Port_Config_GetSecondScreenFloorReturn(void) { return sFloorReturn; }
void Port_Config_SetSecondScreenFloorReturn(bool on) { sFloorReturn = on; SaveConfig(); }
int Port_Config_GetSecondScreenBackdrop(void) { return sBackdrop; }
void Port_Config_SetSecondScreenBackdrop(int style) { sBackdrop = style; SaveConfig(); }
bool Port_Config_GetSecondScreenSwap(void) { return false; }
void Port_Config_SetSecondScreenSwap(bool on) { (void)on; }
/* What the game asks: is the top HUD hidden right now. AUTO hides it exactly while the bottom
 * screen shows the sidebar that has the same things (hearts, buttons, rupees, keys). */
bool Port_Config_GetHideTopHud(void) {
    const int mode = sTopHud;
    return mode == TOP_HUD_HIDE || (mode == TOP_HUD_AUTO && Port_SecondScreen_TopHudCovered());
}
void Port_Config_SetHideTopHud(bool on) { sTopHud = on ? TOP_HUD_HIDE : TOP_HUD_SHOW; SaveConfig(); }
int Port_Config_GetTopHudMode(void) { return (int)sTopHud; }
void Port_Config_CycleTopHudMode(void) {
    sTopHud = (sTopHud + 1) % TOP_HUD_COUNT;
    SaveConfig();
}
float Port_Config_GetPracticeSlowmo(void) { return 1.0f; }
void Port_Config_SetPracticeSlowmo(float v) { (void)v; }
bool Port_Config_GetVSync(void) { return sVsync; }
void Port_Config_SetVSync(bool on) { sVsync = on; }
bool Port_Config_GetColorCorrection(void) { return sColorCorrection; }
void Port_Config_SetColorCorrection(bool on) { sColorCorrection = on; SaveConfig(); }
float Port_Config_GetLcdPersistenceRho(void) { return 0.0f; }
void Port_Config_SetLcdPersistenceRho(float v) { (void)v; }
bool Port_Config_GetHoldToAdvanceText(void) { return sHoldText; }
void Port_Config_SetHoldToAdvanceText(bool on) { sHoldText = on; SaveConfig(); }
float Port_Config_GetMasterVolume(void) { return sVolume; }
void Port_Config_SetMasterVolume(float v) {
    sVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    SaveConfig();
}
const char* Port_Config_GetShaderPreset(void) { return ""; }
void Port_Config_SetShaderPreset(const char* path) { (void)path; }
int Port_Config_HasRebornMask(void) { return 0; }
unsigned Port_Config_GetRebornMask(void) { return 0; }
void Port_Config_SetRebornMask(unsigned mask) { (void)mask; }
void Port_Config_OpenGamepads(void) {}
void Port_Config_CloseGamepads(void) {}

bool Port_Config_InputPressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_InputEdgePressed(PortInput input) {
    const u16 keys = (u16)(~Platform3DS_ReadKeyDownInput()) & 0x03ff;
    static const u16 masks[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return input < 10 && (keys & masks[input]) != 0;
}
bool Port_Config_SoftSlotPressed(int slot) {
    const u32 held = (u32)Platform3DS_KeysHeld();
    static const u32 masks[4] = { 1u << 10, 1u << 11, 1u << 14, 1u << 15 };
    return slot >= 0 && slot < 4 && (held & masks[slot]) != 0;
}
bool Port_Config_GetLeftStick(float* outX, float* outY) {
    float x = 0.0f;
    float y = 0.0f;
    Platform3DS_ReadCircle(&x, &y);
    if (outX) *outX = x;
    if (outY) *outY = y;
    return x != 0.0f || y != 0.0f;
}
float Port_Config_GetAnalogDeadzone(void) { return 0.3f; }
void Port_Config_SetAnalogDeadzone(float v) { (void)v; }
void Port_Config_ClearInputEdges(void) {}
void Port_Config_TestForceEdge(PortInput input) { (void)input; }
void Port_Config_StampInputEdge(PortInput input) { (void)input; }
int Port_Config_PreferredRegion(void) { return -1; }
void Port_Config_SetPreferredRegion(int region) { (void)region; }
int Port_Config_PreferredLanguage(void) { return -1; }
void Port_Config_SetPreferredLanguage(int lang) { (void)lang; }
void Port_Config_SetPortSettingsMenuEnabled(bool enabled) { sPortSettings = enabled; }

bool Port_Config_AutosaveEnabled(void) { return sAutosave; }
void Port_Config_SetAutosaveEnabled(bool enabled) { sAutosave = enabled; SaveConfig(); }
u32 Port_Config_AutosaveIntervalMs(void) { return 300000; }
void Port_Config_SetAutosaveIntervalMs(u32 ms) { (void)ms; }

unsigned Port_Config_GetTurboMultiplier(void) { return sTurboMultiplier; }
void Port_Config_SetTurboMultiplier(unsigned multiplier) {
    if (multiplier < 2) multiplier = 2;
    if (multiplier > 5) multiplier = 5;
    sTurboMultiplier = multiplier;
    Platform3DS_SetTurboMultiplier(multiplier);
    SaveConfig();
}

int Port_Config_Get3DSAspectRatio(void) { return (int)sAspectRatio; }
const char* Port_Config_Get3DSAspectRatioName(void) {
    static const char* const names[PORT_3DS_ASPECT_COUNT] = { "WIDE", "ORIGINAL" };
    return Port_Config_GetWideAi() ? "WIDE + AI" : names[sAspectRatio];
}
bool Port_Config_GetWideAi(void) { return sAspectRatio == PORT_3DS_ASPECT_WIDE && sWideAi; }
/* WIDE is on and the player was never asked whether to show the generated art. */
bool Port_Config_WideAiQuestionDue(void) { return sAspectRatio == PORT_3DS_ASPECT_WIDE && !sWideAiAsked; }
void Port_Config_AnswerWideAi(bool yes) {
    sWideAi = yes;
    sWideAiAsked = true;
    SaveConfig();
}
bool Port_Config_GetAutoUpdate(void) { return sAutoUpdate; }
void Port_Config_ToggleAutoUpdate(void) {
    sAutoUpdate = !sAutoUpdate;
    SaveConfig();
}
void Port_Config_Cycle3DSAspectRatio(void) {
    /* WIDE comes twice: plain, then WIDE + AI (sWideAi); ORIGINAL after it. The way back into WIDE is the
     * plain one. (WIDE + BLUR, between them, was dropped on 2026-10-20: a file's wide_blur is ignored.) */
    if (sAspectRatio == PORT_3DS_ASPECT_WIDE && !sWideAi) {
        sWideAi = true;
        sWideAiAsked = true; /* choosing it is an answer */
    } else {
        sAspectRatio = (Port3DSAspectRatio)((sAspectRatio + 1) % PORT_3DS_ASPECT_COUNT);
        sWideAi = false;
    }
    SaveConfig();
}
int Port_Config_Get3DSDisplayStyle(void) { return (int)sDisplayStyle; }
const char* Port_Config_Get3DSDisplayStyleName(void) {
    return ConfigValues3DS_DisplayLabel(sDisplayStyle);
}
void Port_Config_Cycle3DSDisplayStyle(void) {
    sDisplayStyle = (Port3DSDisplayStyle)((sDisplayStyle + 1) % PORT_3DS_DISPLAY_COUNT);
    SaveConfig();
}
int Port_Config_GetGbaGrid(void) { return (int)sGbaGrid; }
const char* Port_Config_GetGbaGridName(void) {
    static const char* const names[4] = { "OFF", "LOW", "MED", "HIGH" };
    return names[sGbaGrid & 3];
}
void Port_Config_CycleGbaGrid(void) {
    sGbaGrid = (sGbaGrid + 1) & 3;
    SaveConfig();
}
int Port_Config_GetGbaFrame(void) { return (int)sGbaFrame; }
const char* Port_Config_GetGbaFrameName(void) {
    static const char* const names[3] = { "NO", "GBA", "BLUR" };
    return names[sGbaFrame % 3];
}
bool Port_Config_GbaFrameAvailable(void) { return sAspectRatio == PORT_3DS_ASPECT_ORIGINAL; }
int Port_Config_GbaFrameActive(void) { return Port_Config_GbaFrameAvailable() ? (int)sGbaFrame : 0; }
void Port_Config_CycleGbaFrame(void) {
    sGbaFrame = (sGbaFrame + 1) % 3;
    SaveConfig();
}
int Port_Config_GetNeighbourRooms(void) { return (int)sNeighbourRooms; }
const char* Port_Config_GetNeighbourRoomsName(void) {
    return sNeighbourRooms ? "ON" : "OFF";
}
void Port_Config_CycleNeighbourRooms(void) {
    sNeighbourRooms = !sNeighbourRooms;
    SaveConfig();
}
bool Port_Config_3DSFullViewComboEnabled(void) {
    /* Load each atomic once so a bottom-screen option tap can only select one
     * complete policy on this call. A mixed/transient value fails closed on
     * the following scene latch before any 400x240 geometry is published. */
    const Port3DSAspectRatio aspect = sAspectRatio;
    const Port3DSDisplayStyle style = sDisplayStyle;
    return aspect == PORT_3DS_ASPECT_WIDE && style == PORT_3DS_DISPLAY_PIXEL_PERFECT;
}
