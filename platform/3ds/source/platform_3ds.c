#include "fade_gpu_3ds.h"
#include "platform_3ds.h"
#include "boot_text_3ds.h"
#include "retro_ach.h"
#include <math.h>
#include "updater.h"
#include "old3ds_frame_pacer.h"
#include "platform_gpu_3ds.h"
#include "port_audio_3ds.h"
#include "port_second_screen_3ds.h"
#include "port_second_screen_sync_3ds.h"
#include "frame_pacing_3ds.h"
#include "app_paths_3ds.h"
#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_link_3ds.h"
#include "ftp_server_3ds.h"
#endif

#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sHeld;
static uint32_t sDown;
static circlePosition sCirclePosition;
static circlePosition sCStickPosition;
static bool sCStickHeld;
static bool sScreenDumpRequested;
static uint64_t sScreenDumpTick; /* when the last screen dump finished, 0 if none */
static bool sRunning;
static bool sIsNew3DS;
static unsigned sTurboMultiplier = 5;
static bool sCore1Available;
static unsigned sCore1TimeLimit;
/* Retry budget for creating the painter thread. This was a plain bool, so a
 * SINGLE transient threadCreate failure retired the worker permanently and
 * every subsequent paint ran synchronously on the main thread -- 13 ms landing
 * straight on the critical path, every sixth frame, for the rest of the
 * session, with nothing in the logs to say why. Retry with a backoff instead:
 * the failure is usually momentary resource pressure at startup. */
static unsigned sBottomWorkerAttempts;
static unsigned sBottomWorkerRetryIn;
#define BOTTOM_WORKER_MAX_ATTEMPTS 16u
#define BOTTOM_WORKER_RETRY_PAINTS 64u
static bool sBottomWorkerRunning;
static bool sBottomWorkerBusy;
static bool sSpeedupRequested;
static unsigned sTurboPhase;
static uint64_t sLogicFrames;
static uint64_t sPresentedFrames;
static uint64_t sLogicLastTick;
static uint64_t sTurboLogicFrames;
static uint64_t sTurboSkippedPresentations;
static uint64_t sEngineWorkTicks;
static uint64_t sEngineWorkLastTicks;
static uint64_t sEngineWorkMaxTicks;
static uint64_t sVblankWaitTicks;
static uint64_t sVblankWaitLastTicks;
static uint64_t sVblankWaitMaxTicks;
static uint64_t sVblankWaitSamples;
static uint64_t sVblankWaitOverOnePeriod;
static uint64_t sVblankWaitOverTwoPeriods;
static uint64_t sAptChecks;
static uint64_t sFrameBoundaryEndTick;
static Old3DSFramePacer sOld3DSFramePacer;
static uint64_t sOld3DSSkippedTickSleep;
/* FRAMES pacing state (Platform3DS_BeginFrameBoundary). */
static int sLastPacing = -1;
static bool sSkipWaitsVBlank;   /* LOCK 30 skipped this tick: wait one vblank anyway */
static bool sPacerSkipped;      /* the adaptive pacer skipped this tick (sleep, then poll) */
static uint64_t sLock30SkippedPresentations;
static PrintConsole sGameplayLogSink;
static bool sGameplayDisplayActive;
static uintptr_t sStackRegionBase;
static uintptr_t sStackRegionEnd;
typedef struct {
    uintptr_t base;
    uintptr_t end;
} NativeMemoryRegion;
static NativeMemoryRegion sNativeMemoryRegions[8];
static unsigned sNativeMemoryRegionCount;
static Thread sBottomWorkerThread;
static LightEvent sBottomWorkerStart;
static LightEvent sBottomWorkerDone;
static aptHookCookie sAptHookCookie;
static bool sAptHookRegistered;
extern void Port_Audio_Shutdown(void);
static void PollInput(void);

void Platform3DS_MarkFrameDiscontinuity(Old3DSFramePacerDiscontinuity reason) {
    Old3DSFramePacer_MarkDiscontinuity(&sOld3DSFramePacer, reason);
}

static void OnAptEvent(APT_HookType hook, void* parameter) {
    (void)parameter;
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONSLEEP:
        case APTHOOK_ONWAKEUP:
            /* APT is the authority for HOME/lid discontinuities. The atomic
             * mark is consumed at the next logic boundary, so suspend and
             * restore callbacks before that boundary coalesce into one resync. */
            Platform3DS_MarkFrameDiscontinuity(OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
            PlatformGpu3DS_InvalidateBottomTarget();
            /* HOME or the lid is about to take the GPU: drain citro3d's queue
             * while it is still ours (PlatformGpu3DS_DrainQueue). */
            if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) PlatformGpu3DS_DrainQueue();
            break;
        default:
            break;
    }
}

static void RegisterAptHook(void) {
    if (sAptHookRegistered) return;
    aptHook(&sAptHookCookie, OnAptEvent, NULL);
    sAptHookRegistered = true;
}

static bool sPtmuReady;

int Platform3DS_Init(void) {
    Port_SecondScreen_3DS_SyncInit();
    sHeld = 0;
    sDown = 0;
    memset(&sCirclePosition, 0, sizeof(sCirclePosition));
    memset(&sCStickPosition, 0, sizeof(sCStickPosition));
    sCStickHeld = false;
    sScreenDumpRequested = false;
    sScreenDumpTick = 0;
    sCore1Available = false;
    sCore1TimeLimit = 0;
    sSpeedupRequested = false;
    sTurboPhase = 0;
    sLogicFrames = 0;
    sPresentedFrames = 0;
    sLogicLastTick = 0;
    sTurboLogicFrames = 0;
    sTurboSkippedPresentations = 0;
    sEngineWorkTicks = 0;
    sEngineWorkLastTicks = 0;
    sEngineWorkMaxTicks = 0;
    sVblankWaitTicks = 0;
    sVblankWaitLastTicks = 0;
    sVblankWaitMaxTicks = 0;
    sAptChecks = 0;
    sFrameBoundaryEndTick = 0;
    sOld3DSSkippedTickSleep = 0;
    sGameplayDisplayActive = false;
    sStackRegionBase = 0;
    sStackRegionEnd = 0;
    sNativeMemoryRegionCount = 0;
    memset(sNativeMemoryRegions, 0, sizeof(sNativeMemoryRegions));
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);
    cfguInit();
    sPtmuReady = R_SUCCEEDED(ptmuInit());
    romfsInit();
    consoleInit(GFX_BOTTOM, NULL);

    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    APT_CheckNew3DS(&sIsNew3DS);
    Old3DSFramePacer_Init(&sOld3DSFramePacer, SYSCLOCK_ARM11);
    RegisterAptHook();
    if (sIsNew3DS) {
        osSetSpeedupEnable(true);
        sSpeedupRequested = true;
    }

    /* Core 1 share for the app; the remainder goes to the sysmodules, and the
     * one that matters here is GSP, which retires the GX queue that
     * C3D_FrameBegin blocks on. At 80 the sysmodules get 20%, and a starved GSP
     * is the leading explanation for the ~196 ms C3D_FrameBegin waits
     * (citro3d C3Di_WaitAndClearQueue) that coincide with the real frame
     * overruns. Lowering this trades core-1 time away from the PPU worker in
     * exchange for GSP responsiveness; which way that nets out is a hardware
     * question, so it is a knob rather than a new default.
     *
     * `app_cpu_limit=0` (default) keeps the original preference order. */
    Platform3DS_ApplyCpuLimit();

    sRunning = true;
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    hidCircleRead(&sCirclePosition);
    if (sIsNew3DS) hidCstickRead(&sCStickPosition);
    return 1;
}

void Platform3DS_Shutdown(void) {
    sRunning = false;
    if (sAptHookRegistered) {
        aptUnhook(&sAptHookCookie);
        sAptHookRegistered = false;
    }
    Platform3DS_ShutdownBottomWorker();
    extern void virtuappu_mode1_shutdown_workers(void);
    virtuappu_mode1_shutdown_workers();
    Port_Audio_Shutdown();
    romfsExit();
    if (sPtmuReady) ptmuExit();
    sPtmuReady = false;
    cfguExit();
    gfxExit();
}

/* Battery and Wi-Fi for the bottom screen's status icons. The two PTM calls
 * cost a few ms on the console (../sm-3ds measured 2-3 ms), and the level moves
 * far slower than anything else, so they are asked at most every 3 s. */
bool Platform3DS_BatteryStatus(uint8_t* level, uint8_t* charging) {
    static uint64_t lastTick;
    static u8 cachedLevel = 5, cachedCharging;
    if (!sPtmuReady) return false;
    const uint64_t now = svcGetSystemTick();
    if (lastTick == 0 || now - lastTick > 3ull * SYSCLOCK_ARM11) {
        u8 l = 0, c = 0;
        if (R_SUCCEEDED(PTMU_GetBatteryLevel(&l))) cachedLevel = l > 5 ? 5 : l;
        if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&c))) cachedCharging = c;
        lastTick = now;
    }
    *level = cachedLevel;
    *charging = cachedCharging;
    return true;
}

/* Cheap per-frame reads for the perf recorder (Platform3DS_GetRuntimeStats
 * also queries the kernel for memory, too slow to call every frame). */
uint64_t Platform3DS_LogicFrameCount(void) { return sLogicFrames; }
uint64_t Platform3DS_EngineWorkLastTicks(void) { return sEngineWorkLastTicks; }
uint64_t Platform3DS_EngineWorkTotalTicks(void) { return sEngineWorkTicks; }
uint64_t Platform3DS_VblankWaitLastTicks(void) { return sVblankWaitLastTicks; }

int Platform3DS_WifiStrength(void) {
    return (int)osGetWifiStrength();
}

bool Platform3DS_IsRunning(void) { return sRunning; }
bool Platform3DS_IsNew3DS(void) { return sIsNew3DS; }
bool Platform3DS_CanUseCore1(void) { return sCore1Available; }

/* The app's share of core 1 (`app_cpu_limit`, DEBUG -> SWITCHES -> CPU LIMIT).
 * APT accepts a new limit at any time, so the switch applies at once. */
void Platform3DS_ApplyCpuLimit(void) {
    extern int Port_Config_AppCpuLimit(void);
    const int limitCap = Port_Config_AppCpuLimit();
    static const u32 core1Candidates[] = { 80, 70, 50, 30 };
    for (size_t i = 0; i < sizeof(core1Candidates) / sizeof(core1Candidates[0]); ++i) {
        if (limitCap > 0 && core1Candidates[i] > (u32)limitCap) continue;
        if (R_FAILED(APT_SetAppCpuTimeLimit(core1Candidates[i]))) continue;
        u32 actual = 0;
        if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&actual)) && actual > 0) {
            sCore1Available = true;
            sCore1TimeLimit = actual;
            break;
        }
    }
}
unsigned Platform3DS_Core1TimeLimit(void) { return sCore1TimeLimit; }
bool Platform3DS_TurboHeld(void) { return sIsNew3DS && sCStickHeld; }
unsigned Platform3DS_TurboMultiplier(void) { return sTurboMultiplier; }
void Platform3DS_SetTurboMultiplier(unsigned multiplier) {
    sTurboMultiplier = multiplier < 2 ? 2 : (multiplier > 5 ? 5 : multiplier);
}

/* The splash is in 3D while the console's slider is up: the logo hangs in front of the screen like the
 * game's HUD does (stereo_3ds.c: depth 6, half to each eye), the picture shifted the other way in each
 * eye. Turned off again when the boot animation stops, before the game's own presenter takes the top
 * screen over (it links the right eye itself, only while 3D is on). */
static bool sSplash3D;

void Platform3DS_ShowSplash(void) {
    FILE* file = fopen("romfs:/splash.rgb565", "rb");
    uint16_t* pixels = NULL;
    if (file) {
        pixels = (uint16_t*)malloc(400u * 240u * sizeof(uint16_t));
        if (!pixels || fread(pixels, sizeof(uint16_t), 400u * 240u, file) != 400u * 240u) {
            free(pixels);
            pixels = NULL;
        }
        fclose(file);
    }
    if (!pixels) return;

    const float slider = osGet3DSliderState();
    const int shift = (int)(3.0f * slider + 0.5f); /* pixels, the left eye to the right */
    sSplash3D = shift > 0;
    if (sSplash3D) gfxSet3D(true);

    uint16_t width = 0;
    uint16_t height = 0;
    /* Into both buffers of the swap chain, so no later swap shows a black one. */
    for (int pass = 0; pass < 2; ++pass) {
        for (int eye = 0; eye < (sSplash3D ? 2 : 1); ++eye) {
            uint16_t* top = (uint16_t*)gfxGetFramebuffer(GFX_TOP, eye == 0 ? GFX_LEFT : GFX_RIGHT, &width, &height);
            if (!top) goto done;
            const int dx = eye == 0 ? shift : -shift;
            for (int y = 0; y < 240; ++y) {
                for (int x = 0; x < 400; ++x) {
                    const int sx = x - dx;
                    top[(239 - y) + x * 240] = sx >= 0 && sx < 400 ? pixels[y * 400 + sx] : 0;
                }
            }
        }
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, sSplash3D); /* hasStereo: an image for each eye */
        gspWaitForVBlank();
    }
done:
    free(pixels);
}

/* Boot animation on the bottom screen: a gold triforce turning about its vertical axis while
 * the ROM and tables load, in place of the console's text. It draws straight into the gfx
 * framebuffer from its own thread, so it must be stopped before anything else uses the bottom
 * screen (a fatal message's console, or citro3d in Port_PPU_Init). */
static Thread sBootAnimThread;
static volatile bool sBootAnimStop;

static void BootAnimMain(void* arg) {
    (void)arg;
    enum { SIZE = 56, CX = 160, CY = 120 };
    const int x0 = CX - SIZE / 2, y0 = CY - SIZE / 2;
    const float tipH = 0.5f;
    for (unsigned frame = 0; !sBootAnimStop; ++frame) {
        uint16_t w = 0, h = 0;
        uint16_t* fb = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
        if (!fb) break;
        const float c = fabsf(cosf((float)frame * 0.07f));
        const float scale = c < 0.04f ? 0.04f : c;
        const float halfW = (SIZE / 2) * scale;
        for (int y = 0; y < SIZE; ++y) {
            const float v = (y + 0.5f) / SIZE;
            for (int x = 0; x < SIZE; ++x) {
                uint16_t color = 0;
                const float dx = (x + 0.5f) - SIZE / 2;
                if (fabsf(dx) <= halfW) {
                    const float u = 0.5f + dx / (halfW * 2.0f);
                    /* Three upright triangles: the top one and the two at the bottom corners. */
                    bool in;
                    if (v < tipH) {
                        const float half = v * 0.5f / tipH * 0.5f + 0.0f; /* 0..0.25 */
                        in = fabsf(u - 0.5f) <= half;
                    } else {
                        const float t = (v - tipH) / (1.0f - tipH); /* 0..1 down the bottom row */
                        const float half = 0.25f * t;
                        in = fabsf(u - 0.25f) <= half || fabsf(u - 0.75f) <= half;
                    }
                    if (in) {
                        const int shade = 255 - (int)(v * 70.0f);
                        const int r = shade, g = shade * 205 / 255, b = shade * 40 / 255;
                        color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                    }
                }
                fb[(239 - (y0 + y)) + (x0 + x) * 240] = color;
            }
        }
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
}

void Platform3DS_BootAnimStart(void) {
    if (sBootAnimThread) return;
    /* Both buffers of the swap chain start black. */
    for (int i = 0; i < 2; ++i) {
        uint16_t w = 0, h = 0;
        uint16_t* fb = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
        if (fb) memset(fb, 0, 320u * 240u * sizeof(uint16_t));
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
    }
    sBootAnimStop = false;
    sBootAnimThread = threadCreate(BootAnimMain, NULL, 16 * 1024, 0x30, -2, false);
}

void Platform3DS_BootAnimStop(void) {
    if (sSplash3D) {
        gfxSet3D(false);
        sSplash3D = false;
    }
    if (!sBootAnimThread) return;
    sBootAnimStop = true;
    threadJoin(sBootAnimThread, U64_MAX);
    threadFree(sBootAnimThread);
    sBootAnimThread = NULL;
}

static bool GameplayLogSinkPrint(void* console, int character) {
    (void)console;
    (void)character;
    return true;
}

/* From here on stdout and stderr go nowhere: the ROM loader's stderr notes would otherwise be
 * painted on the bottom screen by the boot console, over the boot animation and then over the
 * live UI. stderr stays available to an attached debugger through SVC. */
static void SilenceLogSink(void) {
    static bool done;
    if (done) return;
    fflush(stdout);
    fflush(stderr);
    sGameplayLogSink = *consoleGetDefault();
    sGameplayLogSink.frameBuffer = NULL;
    sGameplayLogSink.PrintChar = GameplayLogSinkPrint;
    sGameplayLogSink.consoleInitialised = true;
    consoleSelect(&sGameplayLogSink);
    consoleDebugInit(debugDevice_SVC);
    done = true;
}

void Platform3DS_SilenceBootConsole(void) { SilenceLogSink(); }

void Platform3DS_EnterGameplayDisplay(void) {
    if (sGameplayDisplayActive) return;
    SilenceLogSink();
    sGameplayDisplayActive = true;
}

static uint16_t MapKeysToGba(uint32_t keys) {
    enum {
        GBA_A = 1u << 0,
        GBA_B = 1u << 1,
        GBA_SELECT = 1u << 2,
        GBA_START = 1u << 3,
        GBA_RIGHT = 1u << 4,
        GBA_LEFT = 1u << 5,
        GBA_UP = 1u << 6,
        GBA_DOWN = 1u << 7,
        GBA_R = 1u << 8,
        GBA_L = 1u << 9,
    };
    uint16_t input = 0x03ff;
    if (keys & KEY_A) input &= ~GBA_A;
    if (keys & KEY_B) input &= ~GBA_B;
    if (keys & KEY_SELECT) input &= ~GBA_SELECT;
    if (keys & KEY_START) input &= ~GBA_START;
    if (keys & (KEY_DRIGHT | KEY_CPAD_RIGHT)) input &= ~GBA_RIGHT;
    if (keys & (KEY_DLEFT | KEY_CPAD_LEFT)) input &= ~GBA_LEFT;
    if (keys & (KEY_DUP | KEY_CPAD_UP)) input &= ~GBA_UP;
    if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) input &= ~GBA_DOWN;
    if (keys & KEY_R) input &= ~GBA_R;
    if (keys & KEY_L) input &= ~GBA_L;
    return input;
}

uint16_t Platform3DS_ReadKeyInput(void) { return MapKeysToGba(sHeld); }
uint16_t Platform3DS_ReadKeyDownInput(void) { return MapKeysToGba(sDown); }

uint32_t Platform3DS_KeysHeld(void) {
    return sHeld;
}

void Platform3DS_ReadCircle(float* x, float* y) {
    if (x) *x = sCirclePosition.dx / 156.0f;
    if (y) *y = -sCirclePosition.dy / 156.0f;
}

uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height) {
    return (uint16_t*)gfxGetFramebuffer(top ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, width, height);
}

uint64_t Platform3DS_Milliseconds(void) {
    return osGetTime();
}

uint64_t Platform3DS_SystemTick(void) {
    return svcGetSystemTick();
}

uint64_t Platform3DS_TicksPerSecond(void) {
    return SYSCLOCK_ARM11;
}

int Platform3DS_IsNativeAddress(uintptr_t value) {
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));

    if (sStackRegionEnd == 0) {
        MemInfo stackInfo;
        PageInfo pageInfo;
        if (R_FAILED(svcQueryMemory(&stackInfo, &pageInfo, (u32)currentSp)) ||
            (stackInfo.perm & MEMPERM_WRITE) == 0u) {
            return 0;
        }
        sStackRegionBase = stackInfo.base_addr;
        sStackRegionEnd = sStackRegionBase + stackInfo.size;
    }

    if (value >= sStackRegionBase && value < sStackRegionEnd) return 1;

    for (unsigned i = 0; i < sNativeMemoryRegionCount; ++i) {
        if (value >= sNativeMemoryRegions[i].base && value < sNativeMemoryRegions[i].end) return 1;
    }

    MemInfo info;
    PageInfo pageInfo;
    if (R_FAILED(svcQueryMemory(&info, &pageInfo, (u32)value))) return 0;
    if (info.base_addr == 0 || info.size == 0) return 0;
    if (value < info.base_addr || value >= info.base_addr + info.size) return 0;
    if ((info.perm & (MEMPERM_READ | MEMPERM_WRITE)) == 0u) return 0;

    if (sNativeMemoryRegionCount < sizeof(sNativeMemoryRegions) / sizeof(sNativeMemoryRegions[0])) {
        NativeMemoryRegion* region = &sNativeMemoryRegions[sNativeMemoryRegionCount++];
        region->base = info.base_addr;
        region->end = info.base_addr + info.size;
    }
    return 1;
}

int Platform3DS_IsActiveStackAddress(uintptr_t value) {
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));

    /* GBA ROM addresses may overlap the 3DS stack reservation numerically.
     * Only objects close to the current frame are unambiguously native stack
     * pointers; keeping this window small preserves normal GBA ROM mapping. */
    const uintptr_t window = 64u * 1024u;
    return value >= currentSp - window && value <= currentSp + window;
}

bool Platform3DS_BeginFrameBoundary(void) {
    const uint64_t now = svcGetSystemTick();
    if (sFrameBoundaryEndTick != 0) {
        sEngineWorkLastTicks = now - sFrameBoundaryEndTick;
        sEngineWorkTicks += sEngineWorkLastTicks;
        if (sEngineWorkLastTicks > sEngineWorkMaxTicks) sEngineWorkMaxTicks = sEngineWorkLastTicks;
    }

    ++sLogicFrames;
    /* One snapshot of the palette buffer and fade parameters per logic frame,
     * for the GPU fade (fade_gpu_3ds.c). */
    FadeGpu3DS_OnFrameBoundary();
    /* OPTIONS -> FRAMES (Port_Config_GetFramePacing). A change resyncs the
     * pacer so time spent in another mode is not taken as debt. */
    const int pacing = Port_Config_GetFramePacing();
    if (pacing != sLastPacing) {
        if (sLastPacing >= 0) Old3DSFramePacer_MarkDiscontinuity(&sOld3DSFramePacer, OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
        sLastPacing = pacing;
    }
    const bool pacerRuns = !sIsNew3DS || pacing == PORT_3DS_PACING_AUTO;
    if (sIsNew3DS && !pacerRuns && sLogicLastTick != 0) {
        /* The pacer is not ticking, but its diagnostic cadence still needs
         * the HOME/sleep/dump filter. */
        Old3DSFramePacer_RecordActiveInterval(&sOld3DSFramePacer, now - sLogicLastTick);
    }
    sLogicLastTick = now;

    if (sIsNew3DS && Platform3DS_TurboHeld()) {
        ++sTurboLogicFrames;
        if (++sTurboPhase >= Platform3DS_TurboMultiplier()) {
            sTurboPhase = 0;
            ++sPresentedFrames;
            return true;
        }
        ++sTurboSkippedPresentations;
        return false;
    }
    sTurboPhase = 0;

    switch (pacing) {
        case PORT_3DS_PACING_NO_SKIP:
            ++sPresentedFrames;
            return true;
        case PORT_3DS_PACING_LOCK30:
            if (sLogicFrames & 1u) {
                /* Not drawn, but still one vblank long: the game stays at 60 Hz. */
                sSkipWaitsVBlank = true;
                ++sLock30SkippedPresentations;
                return false;
            }
            ++sPresentedFrames;
            return true;
        default: {
            const bool present = Old3DSFramePacer_BeginTick(&sOld3DSFramePacer, now, &sOld3DSSkippedTickSleep);
            if (present) ++sPresentedFrames;
            else sPacerSkipped = true;
            return present;
        }
    }
}

void Platform3DS_EndFrameBoundary(void) {
    sFrameBoundaryEndTick = svcGetSystemTick();
}

/* The pump is two calls, and one dump measured it at 8.308 ms/frame average
 * against 0.011 ms in three neighbouring runs -- a 750x swing that accounted
 * for the whole frame deficit (interval 20.901 = vblank 3.040 + work 17.861,
 * of which the pump was 8.308). The low VBlank wait in that run is the effect,
 * not the cause: the pump overran the boundary, so the wait returned instantly.
 *
 * Which of the two calls blocks was not knowable from one counter, and two
 * previous optimisations made on inference rather than measurement paid
 * nothing. So time them separately. aptMainLoop can block on system events;
 * Port_Audio_3DSPump takes a LightLock the audio worker also holds on core 1,
 * which has no priority inheritance and shares that core with the bottom
 * painter and GSP. */
static uint64_t sAptTicks;
static uint64_t sAptMaxTicks;
static uint64_t sAudioPumpTicks;
static uint64_t sAudioPumpMaxTicks;

/* SITE INSTALL (ftp_server_3ds.c) installed a new build of this title: relaunch into it 10 s
 * later, through libctru's chainloader, and let the game close as on HOME -> close. Jumping at
 * once (APT_PrepareToDoApplicationJump + APT_DoApplicationJump) had the system load the title a
 * second time while the new build already ran, and the loader faulted out of memory (Luma
 * dumps 7 and 8, r0 = 0xD86007F3, issue #4); ../legend-of-doom-3ds measured the same: 3 installs
 * in 4 jumping at once, none of 10 waiting 10 s. The notice stays on the bottom screen meanwhile. */
static volatile bool sRelaunchRequested;
static volatile uint64_t sRelaunchAt; /* osGetTime() of the relaunch, 0 none: set on the main thread */

static bool sRelaunching;

void Platform3DS_RequestRelaunch(void) {
    sRelaunchRequested = true;
}

unsigned Platform3DS_RelaunchRemainingMs(void) {
    if (sRelaunchRequested && sRelaunchAt == 0) return 10000u;
    const uint64_t at = sRelaunchAt, now = osGetTime();
    return at == 0 ? 0u : (now >= at ? 0u : (unsigned)(at - now));
}

bool Platform3DS_Relaunching(void) {
    return sRelaunching;
}

static void RelaunchSelf(void) {
    extern void DebugLog_Printf(const char* fmt, ...);
    extern void DebugLog_Flush(void);
    /* The log stays open: main's exit path writes each shutdown step, so a
     * relaunch that never completes shows where it stopped. */
    DebugLog_Printf("relaunching into the build just installed");
    DebugLog_Flush();
    sRelaunching = true;
    /* aptExit() starts this title again once the process has exited, after main's normal exit
     * path (the APT hook drains citro3d's queue, the teardown runs on a thread with a timeout),
     * as HOME -> close and as ../sm-3ds does. Tearing citro3d down *before* a jump hung the
     * console on one relaunch in two (issue #2). */
    aptSetChainloaderToSelf();
}

static bool PumpLifecycleAndAudio(void) {
    if (sRelaunchRequested) {
        extern void DebugLog_Printf(const char* fmt, ...);
        sRelaunchRequested = false;
        sRelaunchAt = osGetTime() + 10000u;
        DebugLog_Printf("new build installed: relaunching in 10 s");
    }
    if (sRelaunchAt != 0 && osGetTime() >= sRelaunchAt) {
        sRelaunchAt = 0;
        RelaunchSelf();
        sRunning = false;
        return false;
    }
    ++sAptChecks;
    const uint64_t aptStart = svcGetSystemTick();
    const bool alive = sRunning && !Updater_ShouldClose() && aptMainLoop();
    const uint64_t aptTicks = svcGetSystemTick() - aptStart;
    sAptTicks += aptTicks;
    if (aptTicks > sAptMaxTicks) sAptMaxTicks = aptTicks;
    if (!alive) {
        sRunning = false;
        return false;
    }
    const uint64_t audioStart = svcGetSystemTick();
    Port_Audio_3DSPump();
    const uint64_t audioTicks = svcGetSystemTick() - audioStart;
    sAudioPumpTicks += audioTicks;
    if (audioTicks > sAudioPumpMaxTicks) sAudioPumpMaxTicks = audioTicks;
    /* Server answers, keep-alive and the unlock notice's timer, also outside gameplay. */
    RetroAch_Update();
    return true;
}

uint64_t Platform3DS_AptTicks(void) { return sAptTicks; }
uint64_t Platform3DS_AptMaxTicks(void) { return sAptMaxTicks; }
uint64_t Platform3DS_AudioPumpTicks(void) { return sAudioPumpTicks; }
uint64_t Platform3DS_AudioPumpMaxTicks(void) { return sAudioPumpMaxTicks; }
uint64_t Platform3DS_VblankWaitSamples(void) { return sVblankWaitSamples; }
uint64_t Platform3DS_VblankWaitOverOnePeriod(void) { return sVblankWaitOverOnePeriod; }
uint64_t Platform3DS_VblankWaitOverTwoPeriods(void) { return sVblankWaitOverTwoPeriods; }

void Platform3DS_PumpWithoutVBlank(void) {
    if (!PumpLifecycleAndAudio()) return;
    if (sSkipWaitsVBlank) {
        /* LOCK 30: nothing new to show, so no swap; the presenter keeps the
         * last picture while the game ticks on the vblank. */
        sSkipWaitsVBlank = false;
        gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
        PollInput();
        return;
    }
    /* New 3DS turbo skips run as fast as they can; only a pacer skip sleeps. */
    if (sIsNew3DS && !sPacerSkipped) return;
    sPacerSkipped = false;

    if (sOld3DSSkippedTickSleep != 0) {
        const uint64_t sleepNs = sOld3DSSkippedTickSleep * 1000000000ULL / SYSCLOCK_ARM11;
        sOld3DSSkippedTickSleep = 0;
        if (sleepNs != 0) svcSleepThread((int64_t)sleepNs);
    }

    PollInput();
}

void Platform3DS_GetRuntimeStats(Platform3DSRuntimeStats* stats) {
    if (!stats) return;
    s32 threadPriority = -1;
    uintptr_t currentSp;
    __asm__ volatile("mov %0, sp" : "=r"(currentSp));
    svcGetThreadPriority(&threadPriority, CUR_THREAD_HANDLE);
    if (sStackRegionEnd == 0) Platform3DS_IsNativeAddress(currentSp);
    *stats = (Platform3DSRuntimeStats){
        .kernelVersion = osGetKernelVersion(),
        .firmVersion = osGetFirmVersion(),
        .systemCoreVersion = osGetSystemCoreVersion(),
        .applicationMemoryType = osGetApplicationMemType(),
        .mainThreadPriority = threadPriority,
        .applicationMemoryFree = osGetMemRegionFree(MEMREGION_APPLICATION),
        .systemMemoryFree = osGetMemRegionFree(MEMREGION_SYSTEM),
        .baseMemoryFree = osGetMemRegionFree(MEMREGION_BASE),
        .linearMemoryFree = linearSpaceFree(),
        .currentStackPointer = currentSp,
        .stackRegionBase = sStackRegionBase,
        .stackRegionEnd = sStackRegionEnd,
        .logicFrames = sLogicFrames,
        .presentedFrames = sPresentedFrames,
        .logicElapsedTicks = sOld3DSFramePacer.activeElapsedTicks,
        .logicCadenceIntervals = sOld3DSFramePacer.activeIntervals,
        .turboLogicFrames = sTurboLogicFrames,
        .turboSkippedPresentations = sTurboSkippedPresentations,
        .old3dsSkippedPresentations = sOld3DSFramePacer.skippedPresentations,
        .old3dsPacingSleepTicks = sOld3DSFramePacer.requestedSleepTicks,
        .old3dsPacingResyncs = sOld3DSFramePacer.resyncs,
        .old3dsAptDiscontinuities = sOld3DSFramePacer.aptDiscontinuities,
        .old3dsDumpDiscontinuities = sOld3DSFramePacer.dumpDiscontinuities,
        .old3dsDebtClampEvents = sOld3DSFramePacer.debtClampEvents,
        .old3dsPresentationDebtTicks = sOld3DSFramePacer.presentationDebtTicks,
        .old3dsMaxConsecutiveSkips = sOld3DSFramePacer.maxConsecutiveSkipsSeen,
        .engineWorkTicks = sEngineWorkTicks,
        .engineWorkLastTicks = sEngineWorkLastTicks,
        .engineWorkMaxTicks = sEngineWorkMaxTicks,
        .vblankWaitTicks = sVblankWaitTicks,
        .vblankWaitLastTicks = sVblankWaitLastTicks,
        .vblankWaitMaxTicks = sVblankWaitMaxTicks,
        .aptChecks = sAptChecks,
        .keyMaskHeld = sHeld,
        .keyMaskDown = sDown,
        .circleX = sCirclePosition.dx,
        .circleY = sCirclePosition.dy,
        .cstickX = sCStickPosition.dx,
        .cstickY = sCStickPosition.dy,
        .turboHeld = Platform3DS_TurboHeld(),
        .bottomWorkerRunning = sBottomWorkerRunning,
        .bottomWorkerBusy = sBottomWorkerBusy,
        .speedupRequested = sSpeedupRequested,
        .adaptiveFrameskipEnabled = !sIsNew3DS,
        .gameplayDisplayActive = sGameplayDisplayActive,
        .aptCloseRequested = aptShouldClose(),
    };
}

static void BottomWorkerMain(void* argument) {
    (void)argument;
    for (;;) {
        LightEvent_Wait(&sBottomWorkerStart);
        if (!__atomic_load_n(&sBottomWorkerRunning, __ATOMIC_ACQUIRE)) break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        extern void Port_PPU_3DS_RenderBottomWorker(void);
        Port_PPU_3DS_RenderBottomWorker();
        LightEvent_Signal(&sBottomWorkerDone);
    }
}

static bool EnsureBottomWorker(void) {
    if (sBottomWorkerThread) return true;
    if (sBottomWorkerAttempts >= BOTTOM_WORKER_MAX_ATTEMPTS) return false;
    if (sBottomWorkerRetryIn != 0) {
        --sBottomWorkerRetryIn;
        return false;
    }
    ++sBottomWorkerAttempts;
    sBottomWorkerRetryIn = BOTTOM_WORKER_RETRY_PAINTS;

    LightEvent_Init(&sBottomWorkerStart, RESET_ONESHOT);
    LightEvent_Init(&sBottomWorkerDone, RESET_ONESHOT);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (priority < 0x3e) priority += 2;
    sBottomWorkerRunning = true;
    /* The painter has always shared core 0 with the main thread. It cannot
     * preempt it (priority 50 against 48), but it does consume core 0 cycles,
     * and its 83 ms spikes coincide with the frames that miss VBlank. Core 1
     * carries audio and GSP but has idle time, and this thread is low priority
     * so it would only take what is going spare. Switchable rather than
     * assumed: moving audio to core 0 was tried before and made things worse.
     * AUTO is core 1 on the Old 3DS (2026-10-13, 2DS in Full View): on core 0 the
     * main thread left it so little that a paint took 350-550 ms of wall time
     * (the R label, hearts and taps lagged by up to a second); on core 1, 70 ms
     * with the same frame rate. The New 3DS keeps core 0 (not measured). */
    extern int Port_Config_BottomCore(void);
    const int bottomOverride = Port_Config_BottomCore();
    const int core = (bottomOverride == 0 || bottomOverride == 1) ? bottomOverride
                                                                  : (Platform3DS_IsNew3DS() ? 0 : 1);
    sBottomWorkerThread = threadCreate(BottomWorkerMain, NULL, 64u * 1024u, priority, core, false);
    if (!sBottomWorkerThread) {
        sBottomWorkerRunning = false;
        char line[128];
        snprintf(line, sizeof(line),
                 "[tmc3ds] bottom worker create failed (attempt %u/%u); retrying in %u paints\n",
                 sBottomWorkerAttempts, BOTTOM_WORKER_MAX_ATTEMPTS,
                 BOTTOM_WORKER_RETRY_PAINTS);
        Platform3DS_Debug(line);
    }
    return sBottomWorkerThread != NULL;
}

/* DEBUG -> SWITCHES -> BOTTOM CORE: the painter thread is recreated on the
 * other core before its next paint. Only between paints: a thread stopped
 * mid-paint would leave its done event to a fresh LightEvent_Init. */
static volatile bool sBottomWorkerRestart;

void Platform3DS_RestartBottomWorker(void) {
    sBottomWorkerRestart = true;
}

bool Platform3DS_SubmitBottomWorker(void) {
    if (sBottomWorkerBusy) return false;
    if (sBottomWorkerRestart) {
        sBottomWorkerRestart = false;
        Platform3DS_ShutdownBottomWorker();
        sBottomWorkerAttempts = 0;
        sBottomWorkerRetryIn = 0;
    }
    if (!EnsureBottomWorker()) return false;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    sBottomWorkerBusy = true;
    LightEvent_Signal(&sBottomWorkerStart);
    return true;
}

bool Platform3DS_TryFinishBottomWorker(void) {
    if (!sBottomWorkerBusy || !LightEvent_TryWait(&sBottomWorkerDone)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    sBottomWorkerBusy = false;
    return true;
}

void Platform3DS_ShutdownBottomWorker(void) {
    if (!sBottomWorkerThread) return;
    __atomic_store_n(&sBottomWorkerRunning, false, __ATOMIC_RELEASE);
    LightEvent_Signal(&sBottomWorkerStart);
    threadJoin(sBottomWorkerThread, 2000000000ULL);
    threadFree(sBottomWorkerThread);
    sBottomWorkerThread = NULL;
    sBottomWorkerRunning = false;
    sBottomWorkerBusy = false;
    /* A deliberate shutdown is not a failure, so the retry budget resets. */
    sBottomWorkerAttempts = 0;
    sBottomWorkerRetryIn = 0;
}

static void PollInput(void) {
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
#if TMC3DS_DEBUG_TOOLS
    DebugLink_Poll(&sHeld, &sDown, sLogicFrames); /* remote buttons and requests (SITE over FTP) */
#endif
    hidCircleRead(&sCirclePosition);
    if (sIsNew3DS) {
        hidCstickRead(&sCStickPosition);
        sCStickHeld = (sHeld & (KEY_CSTICK_UP | KEY_CSTICK_DOWN | KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT)) != 0u ||
                      abs((int)sCStickPosition.dx) > 24 || abs((int)sCStickPosition.dy) > 24;
    } else {
        memset(&sCStickPosition, 0, sizeof(sCStickPosition));
        sCStickHeld = false;
    }
    Port_SecondScreen_3DS_ServicePendingTap(); /* a tap kept while its panel was repainted */
    if (sDown & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        Port_SecondScreen_3DS_OnTap(touch.px, touch.py, 0);
    }
}

/* The main thread can stop advancing while audio keeps playing -- both screens
 * hold their last contents and no quick dump can be taken, because the dump
 * runs on the main thread. These two let a thread that is still alive report
 * where the main thread stopped. */
static volatile uint32_t sMainStage;
static volatile uint32_t sMainHeartbeat;

void Platform3DS_SetStage(uint32_t stage) { sMainStage = stage; }
void Platform3DS_Heartbeat(void) { ++sMainHeartbeat; }

void Platform3DS_WatchdogPoll(void) {
#if TMC3DS_DEBUG_TOOLS
    FtpServer_WatchdogPoll();
#endif
    static uint32_t lastHeartbeat;
    static unsigned stalledPolls;
    static bool reported;
    const uint32_t beat = sMainHeartbeat;
    if (beat != lastHeartbeat) {
        lastHeartbeat = beat;
        stalledPolls = 0;
        reported = false;
        return;
    }
    if (reported) return;
    /* Audio wakes roughly every 16 ms, so a few hundred polls is several
     * seconds of a genuinely stopped main thread rather than a slow frame. */
    if (++stalledPolls < 240u) return;
    reported = true;
    char line[192], ftp[64] = "not built";
#if TMC3DS_DEBUG_TOOLS
    FtpServer_DescribeStage(ftp, sizeof(ftp));
#endif
    snprintf(line, sizeof(line),
             "[tmc3ds] WATCHDOG: main thread stopped at stage %lu, frame %lu (ftp server: %s)\n",
             (unsigned long)sMainStage, (unsigned long)beat, ftp);
    Platform3DS_Debug(line);
}

/* GSPGPU_FlushDataCache is a round trip to the GSP sysmodule: the caller
 * blocks, and GSP does the work on core 1 -- the same core the audio worker
 * runs on, with only the app's quota share of it. svcStoreProcessDataCache
 * cleans the same lines from this thread with no service call and no core-1
 * wakeup. Whether it is permitted depends on how the title was launched, so it
 * is probed once and the GSP path stays as the fallback. */
static int sCacheCleanMode; /* 0 unprobed, 1 direct SVC, 2 GSP fallback */

bool Platform3DS_CleanDataCache(const void* addr, size_t size) {
    if (!addr || size == 0) return true;
    if (sCacheCleanMode == 0) {
        const Result probe = svcStoreProcessDataCache(
                CUR_PROCESS_HANDLE, (u32)(uintptr_t)addr, (u32)size);
        sCacheCleanMode = R_SUCCEEDED(probe) ? 1 : 2;
        char line[96];
        snprintf(line, sizeof(line),
                 "[tmc3ds] cache clean: %s (probe 0x%08lx)\n",
                 sCacheCleanMode == 1 ? "direct SVC" : "GSP fallback",
                 (unsigned long)probe);
        Platform3DS_Debug(line);
        if (sCacheCleanMode == 1) return true;
    }
    if (sCacheCleanMode == 1) {
        return R_SUCCEEDED(svcStoreProcessDataCache(
                CUR_PROCESS_HANDLE, (u32)(uintptr_t)addr, (u32)size));
    }
    /* Fallback. GSPGPU_FlushDataCache is a synchronous service call: it blocks
     * this thread until GSP has done the work. GX_FlushCacheRegions queues the
     * same request onto the GX command queue instead, so the caller carries on.
     * The work still runs on core 1 either way -- this buys main-thread time,
     * not audio time. */
    return R_SUCCEEDED(GX_FlushCacheRegions((u32*)(uintptr_t)addr, (u32)size,
                                            NULL, 0, NULL, 0));
}

const char* Platform3DS_CacheCleanPath(void) {
    return sCacheCleanMode == 1   ? "direct SVC (no GSP round trip)"
           : sCacheCleanMode == 2 ? "GSP fallback"
                                  : "unprobed";
}

#if TMC3DS_DEBUG_TOOLS
/* The DEBUG tab's SCREEN DUMP and the link's DUMP only ask; the dump itself
 * runs at the frame boundary (WaitForVBlank, below), where nothing of the frame
 * is half done. There is no button chord any more (it was upstream's L+R+A, in
 * every build). */
void Platform3DS_RequestScreenDump(void) { sScreenDumpRequested = true; }
#else
void Platform3DS_RequestScreenDump(void) {}
#endif

uint32_t Platform3DS_SecondsSinceScreenDump(void) {
    if (sScreenDumpTick == 0) return 0xffffffffu;
    return (uint32_t)((svcGetSystemTick() - sScreenDumpTick) / Platform3DS_TicksPerSecond());
}

/* Render and engine work together account for 12.7 ms of a 20.4 ms frame, and
 * the VBlank wait is 7.7 ms where about 4 would be expected -- so a few
 * milliseconds are spent somewhere neither counter watches. These cover the
 * rest of this function: the lifecycle/audio pump before the wait, and the
 * second-screen promotion and input scan after it. */
static uint64_t sPumpTicks, sPostWaitTicks;
static uint64_t sPumpMaxTicks, sPostWaitMaxTicks;

uint64_t Platform3DS_PumpTicks(void) { return sPumpTicks; }
uint64_t Platform3DS_PumpMaxTicks(void) { return sPumpMaxTicks; }
uint64_t Platform3DS_PostWaitTicks(void) { return sPostWaitTicks; }
uint64_t Platform3DS_PostWaitMaxTicks(void) { return sPostWaitMaxTicks; }

void Platform3DS_WaitForVBlank(void) {
    Platform3DS_SetStage(11);
    const uint64_t pumpStart = svcGetSystemTick();
    const bool pumped = PumpLifecycleAndAudio();
    const uint64_t pumpTicks = svcGetSystemTick() - pumpStart;
    sPumpTicks += pumpTicks;
    if (pumpTicks > sPumpMaxTicks) sPumpMaxTicks = pumpTicks;
    if (!pumped) return;
    extern bool Port_PPU_3DS_UsesGpuPresenter(void);
    extern bool Port_Config_VblankPhaseLock(void);
    const uint64_t waitStart = svcGetSystemTick();
    if (Port_PPU_3DS_UsesGpuPresenter()) {
        Platform3DS_SetStage(12);
        /* nextEvent=false does not discard an already-pending VBlank, so a
         * frame whose work crossed one returns from here immediately, presents
         * mid-scanout, and the loop drifts out of phase with the display. That
         * matches the measured shape: 79.8% of intervals exceed 16.67 ms while
         * only 1.07% exceed 33.33 ms, and a phase-locked loop can only emit
         * multiples of 16.71 ms. Real work is 7.8 ms of the 16.71 ms period, so
         * the deficit is phase, not throughput.
         *
         * nextEvent=true phase-locks to the next VBlank. The risk is the
         * mirror image: any frame that genuinely overruns a period then always
         * waits a full extra one, which can pin a heavy scene to 30 FPS. That
         * trade is what the frame pacer arbitrates, so this is a switch to be
         * A/B'd against it on hardware, not a fix to assume. */
        gspWaitForEvent(GSPGPU_EVENT_VBlank0, Port_Config_VblankPhaseLock());
        Platform3DS_SetStage(13);
    } else {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    sVblankWaitLastTicks = svcGetSystemTick() - waitStart;
    sVblankWaitTicks += sVblankWaitLastTicks;
    if (sVblankWaitLastTicks > sVblankWaitMaxTicks) sVblankWaitMaxTicks = sVblankWaitLastTicks;
    /* Directly count lost display periods instead of inferring them.
     *
     * The whole residual deficit is that the loop waits ~2.1 ms/frame longer
     * than geometry predicts: work is 7.8 ms of a 16.74 ms period, so the wait
     * should be ~8.9 ms and measures ~11.0 ms. 2.1 / 16.7 = ~13%, which reads
     * as "13% of iterations lose a full period" -- but that was deduced from
     * interval arithmetic, and the interval counters disagree with it (only
     * 2.83% of intervals exceed two periods). One of the two is wrong.
     *
     * A wait longer than one period is unambiguous: with 7.8 ms of work the
     * next VBlank is always less than a period away, so exceeding one period
     * means the boundary was missed and the loop caught a later one. */
    ++sVblankWaitSamples;
    {
        const uint64_t period = Platform3DS_TicksPerSecond() * 280896u / 16777216u;
        if (sVblankWaitLastTicks > period) ++sVblankWaitOverOnePeriod;
        if (sVblankWaitLastTicks > period * 2u) ++sVblankWaitOverTwoPeriods;
    }
    /* EndBottom only marks a bottom generation submitted.  Promote it after
     * the display boundary and before this tick's HID scan, so touch never
     * targets a CPU-painted buffer that has not reached a presentation. */
    const uint64_t postStart = svcGetSystemTick();
    Port_SecondScreen_3DS_PromoteSubmitted();
    if (sScreenDumpRequested) {
        extern void Port_PPU_3DS_WriteScreenDump(void);
        sScreenDumpRequested = false;
        Port_PPU_3DS_WriteScreenDump();
        sScreenDumpTick = svcGetSystemTick();
    }
    PollInput();
    const uint64_t postTicks = svcGetSystemTick() - postStart;
    sPostWaitTicks += postTicks;
    if (postTicks > sPostWaitMaxTicks) sPostWaitMaxTicks = postTicks;
}

void Platform3DS_ShowFatal(const char* title, const char* message) {
    /* Also from inside Port_LoadRom, while the boot animation still draws there. */
    Platform3DS_BootAnimStop();
    BootText_ShowMessage(BootText_Tr(title ? title : "ERROR"), BOOT_TEXT_BAD, message, BootText_Tr("PRESS START TO EXIT."));
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

void Platform3DS_Debug(const char* message) {
    if (!message) return;
    svcOutputDebugString(message, strlen(message));
    FILE* file = fopen("tmc3ds.log", "ab");
    if (file) {
        fwrite(message, 1, strlen(message), file);
        fclose(file);
    }
}

static void ReadFramebufferPixel(const uint8_t* pixel, GSPGPU_FramebufferFormat format,
                                 uint8_t* red, uint8_t* green, uint8_t* blue) {
    switch (format) {
        case GSP_RGB565_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 5) & 63u) * 255u / 63u);
            *blue = (uint8_t)((color & 31u) * 255u / 31u);
            break;
        }
        case GSP_BGR8_OES:
            *blue = pixel[0];
            *green = pixel[1];
            *red = pixel[2];
            break;
        case GSP_RGBA8_OES:
            *red = pixel[0];
            *green = pixel[1];
            *blue = pixel[2];
            break;
        case GSP_RGB5_A1_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 6) & 31u) * 255u / 31u);
            *blue = (uint8_t)(((color >> 1) & 31u) * 255u / 31u);
            break;
        }
        case GSP_RGBA4_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 12) & 15u) * 17u);
            *green = (uint8_t)(((color >> 8) & 15u) * 17u);
            *blue = (uint8_t)(((color >> 4) & 15u) * 17u);
            break;
        }
        default:
            *red = *green = *blue = 0;
            break;
    }
}

static bool SaveCapturedFramebufferBmp(const char* path, const GSPGPU_CaptureInfoEntry* capture,
                                       int width, int height) {
    if (!path || !capture || !capture->framebuf0_vaddr) return false;
    const GSPGPU_FramebufferFormat format = (GSPGPU_FramebufferFormat)(capture->format & 7u);
    const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
    if (bytesPerPixel < 2 || bytesPerPixel > 4 || capture->framebuf_widthbytesize == 0) return false;

    const uint8_t* framebuffer = (const uint8_t*)capture->framebuf0_vaddr;
    GSPGPU_InvalidateDataCache(framebuffer, capture->framebuf_widthbytesize * (u32)width);

    const int rowSize = (width * 3 + 3) & ~3;
    const uint32_t fileSize = 54u + (uint32_t)rowSize * (uint32_t)height;
    /* Built in RAM and written with one fwrite: row by row, the SD card took
     * about 5 s for the two screens, with the game frozen meanwhile. */
    uint8_t* image = calloc(1, fileSize);
    if (!image) return false;
    const uint8_t header[54] = {
        'B', 'M',
        (uint8_t)fileSize, (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)width, (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)height, (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0, 24, 0,
    };
    memcpy(image, header, sizeof(header));
    for (int y = height - 1; y >= 0; --y) {
        uint8_t* row = image + 54 + (size_t)(height - 1 - y) * (size_t)rowSize;
        for (int x = 0; x < width; ++x) {
            const uint8_t* pixel = framebuffer + (size_t)x * capture->framebuf_widthbytesize +
                                   (size_t)(height - 1 - y) * bytesPerPixel;
            uint8_t red, green, blue;
            ReadFramebufferPixel(pixel, format, &red, &green, &blue);
            row[x * 3 + 0] = blue;
            row[x * 3 + 1] = green;
            row[x * 3 + 2] = red;
        }
    }
    FILE* file = fopen(path, "wb");
    bool ok = file && fwrite(image, 1, fileSize, file) == fileSize;
    if (file && fclose(file) != 0) ok = false;
    free(image);
    return ok;
}

/* The top screen as shown, scaled to width x height RGB565 (each output pixel
 * the average of its block), for save-state screenshots. */
bool Platform3DS_CaptureTopThumbnail(uint16_t* pixels, int width, int height) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (!pixels || width <= 0 || height <= 0 || R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    const GSPGPU_CaptureInfoEntry* top = &capture.screencapture[GSP_SCREEN_TOP];
    if (!top->framebuf0_vaddr || top->framebuf_widthbytesize == 0) return false;
    const GSPGPU_FramebufferFormat format = (GSPGPU_FramebufferFormat)(top->format & 7u);
    const unsigned bpp = gspGetBytesPerPixel(format);
    if (bpp < 2 || bpp > 4) return false;
    const int srcW = 400, srcH = 240;
    const uint8_t* fb = (const uint8_t*)top->framebuf0_vaddr; /* column-major, origin bottom-left */
    GSPGPU_InvalidateDataCache(fb, top->framebuf_widthbytesize * (u32)srcW);
    const int bw = srcW / width > 0 ? srcW / width : 1, bh = srcH / height > 0 ? srcH / height : 1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < bh; ++dy) {
                for (int dx = 0; dx < bw; ++dx) {
                    const int sx = x * srcW / width + dx, sy = srcH - 1 - (y * srcH / height + dy);
                    uint8_t pr, pg, pb;
                    ReadFramebufferPixel(fb + (size_t)sx * top->framebuf_widthbytesize + (size_t)sy * bpp, format,
                                         &pr, &pg, &pb);
                    r += pr;
                    g += pg;
                    b += pb;
                }
            }
            const unsigned n = (unsigned)(bw * bh);
            r /= n;
            g /= n;
            b /= n;
            pixels[y * width + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    return true;
}

/* The top screen as shown, full size for the scene recorder: 400 columns of 240 pixels in the
 * framebuffer's own order (column-major, each column bottom to top), as RGB565. */
bool Platform3DS_CaptureTopRgb565(uint16_t* pixels) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (!pixels || R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    const GSPGPU_CaptureInfoEntry* top = &capture.screencapture[GSP_SCREEN_TOP];
    if (!top->framebuf0_vaddr || top->framebuf_widthbytesize == 0) return false;
    const GSPGPU_FramebufferFormat format = (GSPGPU_FramebufferFormat)(top->format & 7u);
    const unsigned bpp = gspGetBytesPerPixel(format);
    if (bpp < 2 || bpp > 4 || top->framebuf_widthbytesize < 240u * bpp) return false;
    const uint8_t* fb = (const uint8_t*)top->framebuf0_vaddr;
    GSPGPU_InvalidateDataCache(fb, top->framebuf_widthbytesize * 400u);
    for (unsigned col = 0; col < 400u; ++col) {
        const uint8_t* src = fb + (size_t)col * top->framebuf_widthbytesize;
        uint16_t* dst = pixels + (size_t)col * 240u;
        if (format == GSP_RGB565_OES) {
            memcpy(dst, src, 240u * 2u);
        } else if (format == GSP_BGR8_OES) {
            for (unsigned i = 0; i < 240u; ++i, src += 3)
                dst[i] = (uint16_t)(((src[2] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[0] >> 3));
        } else {
            for (unsigned i = 0; i < 240u; ++i, src += bpp) {
                uint8_t r, g, b;
                ReadFramebufferPixel(src, format, &r, &g, &b);
                dst[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
    return true;
}

static bool SaveCapturedFramebufferRaw(const char* path, const GSPGPU_CaptureInfoEntry* capture,
                                       int width) {
    if (!path) return true;
    if (!capture || !capture->framebuf0_vaddr || capture->framebuf_widthbytesize == 0) return false;
    const size_t size = (size_t)capture->framebuf_widthbytesize * (size_t)width;
    GSPGPU_InvalidateDataCache(capture->framebuf0_vaddr, size);
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(capture->framebuf0_vaddr, 1, size, file) == size;
    if (fclose(file) != 0) return false;
    return ok;
}

bool Platform3DS_SaveDisplayedScreensDetailed(const char* topPath, const char* bottomPath,
                                              const char* topRawPath, const char* bottomRawPath,
                                              Platform3DSCaptureStats* stats) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    const GSPGPU_CaptureInfoEntry* top = &capture.screencapture[GSP_SCREEN_TOP];
    const GSPGPU_CaptureInfoEntry* bottom = &capture.screencapture[GSP_SCREEN_BOTTOM];
    if (stats) {
        *stats = (Platform3DSCaptureStats){
            .topFormat = top->format & 7u,
            .topStride = top->framebuf_widthbytesize,
            .bottomFormat = bottom->format & 7u,
            .bottomStride = bottom->framebuf_widthbytesize,
            .topAddress = (uintptr_t)top->framebuf0_vaddr,
            .bottomAddress = (uintptr_t)bottom->framebuf0_vaddr,
        };
    }
    const bool topOk = SaveCapturedFramebufferBmp(topPath, top, 400, 240);
    const bool bottomOk = SaveCapturedFramebufferBmp(bottomPath, bottom, 320, 240);
    const bool topRawOk = SaveCapturedFramebufferRaw(topRawPath, top, 400);
    const bool bottomRawOk = SaveCapturedFramebufferRaw(bottomRawPath, bottom, 320);
    return topOk && bottomOk && topRawOk && bottomRawOk;
}

/* The top screen's right eye as shown, when the system has a second framebuffer for it (3D on): false otherwise. */
bool Platform3DS_SaveTopRightEye(const char* path) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    GSPGPU_CaptureInfoEntry top = capture.screencapture[GSP_SCREEN_TOP];
    if (!top.framebuf1_vaddr || top.framebuf1_vaddr == top.framebuf0_vaddr) return false;
    top.framebuf0_vaddr = top.framebuf1_vaddr;
    return SaveCapturedFramebufferBmp(path, &top, 400, 240);
}

bool Platform3DS_SaveDisplayedScreens(const char* topPath, const char* bottomPath) {
    return Platform3DS_SaveDisplayedScreensDetailed(topPath, bottomPath, NULL, NULL, NULL);
}
