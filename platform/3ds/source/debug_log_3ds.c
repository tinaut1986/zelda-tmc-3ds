#include "debug_log_3ds.h"

#include "debug_files_3ds.h"
#include "platform_3ds.h"
#include "port_ppu_gpu_3ds.h"
#include "port_ppu_gpu_3ds_model.h"
#include "port_second_screen_state.h"
#include "tmc3ds_version.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { LOG_BUFFER_SIZE = 16 * 1024, PERF_LINE_SECONDS = 5 };

static LightLock sLock;
static bool sLockReady;
static FILE* sFile;
static int sNumber;
static bool sDirect;
static char sBuffer[LOG_BUFFER_SIZE];
static unsigned sUsed;
static int sMarks;

extern double Port_PPU_3DS_CurrentFps(void);
extern double Port_PPU_3DS_AverageFps(void);

static void Lock(void) {
    if (!sLockReady) {
        LightLock_Init(&sLock);
        sLockReady = true;
    }
    LightLock_Lock(&sLock);
}

static void Unlock(void) {
    LightLock_Unlock(&sLock);
}

static void FlushLocked(void) {
    if (sFile && sUsed) {
        fwrite(sBuffer, 1, sUsed, sFile);
        fflush(sFile);
    }
    sUsed = 0;
}

static void WriteLocked(const char* text, size_t n) {
    if (!sFile) return;
    if (sDirect) {
        fwrite(text, 1, n, sFile);
        fflush(sFile);
        return;
    }
    if (sUsed + n > sizeof(sBuffer)) FlushLocked();
    if (n > sizeof(sBuffer)) {
        fwrite(text, 1, n, sFile);
        return;
    }
    memcpy(sBuffer + sUsed, text, n);
    sUsed += (unsigned)n;
}

static void VPrintfLocked(const char* fmt, va_list args) {
    char line[512];
    const double t = (double)Platform3DS_Milliseconds() / 1000.0;
    int head = snprintf(line, sizeof(line), "[%9.3f] ", t);
    int body = vsnprintf(line + head, sizeof(line) - (size_t)head - 1, fmt, args);
    if (body < 0) return;
    size_t n = (size_t)head + (size_t)body;
    if (n > sizeof(line) - 2) n = sizeof(line) - 2;
    if (line[n - 1] != '\n') line[n++] = '\n';
    WriteLocked(line, n);
}

static void PrintfLocked(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VPrintfLocked(fmt, args);
    va_end(args);
}

void DebugLog_Printf(const char* fmt, ...) {
    if (!sFile) return;
    Lock();
    va_list args;
    va_start(args, fmt);
    VPrintfLocked(fmt, args);
    va_end(args);
    Unlock();
}

/* port_config_3ds.c: every setting, then each change as it is saved. */
extern void Port_Config_3DSLogSettings(void);

void DebugLog_Start(void) {
    Lock();
    const bool opened = !sFile;
    if (!sFile) {
        char path[96];
        sNumber = DebugFiles_Next("log", ".txt", path, sizeof(path));
        sFile = sNumber > 0 ? fopen(path, "w") : NULL;
        if (!sFile) sNumber = 0;
        sUsed = 0;
        sMarks = 0;
        if (sFile) {
            Platform3DSRuntimeStats rt;
            Platform3DS_GetRuntimeStats(&rt);
            PrintfLocked("Minish Cap 3DS v%s (upstream %s), log %04d, %s", TMC_PORT_VERSION_LABEL,
                         TMC_UPSTREAM_VERSION, sNumber, sDirect ? "direct" : "buffered");
            PrintfLocked("console %s, core 1 limit %u%%, kernel %08lx, app memory free %lu KB, linear free %lu KB",
                         Platform3DS_IsNew3DS() ? "New 3DS" : "Old 3DS", Platform3DS_Core1TimeLimit(),
                         (unsigned long)rt.kernelVersion, (unsigned long)(rt.applicationMemoryFree / 1024),
                         (unsigned long)(rt.linearMemoryFree / 1024));
            FlushLocked();
        }
    }
    Unlock();
    if (opened && sFile) Port_Config_3DSLogSettings();
}

void DebugLog_Stop(void) {
    Lock();
    if (sFile) {
        PrintfLocked("log stopped");
        FlushLocked();
        fclose(sFile);
        sFile = NULL;
    }
    Unlock();
}

bool DebugLog_Running(void) {
    return sFile != NULL;
}

void DebugLog_SetDirect(bool direct) {
    Lock();
    FlushLocked();
    sDirect = direct;
    if (sFile) PrintfLocked("mode: %s", direct ? "direct" : "buffered");
    Unlock();
}

bool DebugLog_Direct(void) {
    return sDirect;
}

int DebugLog_FileNumber(void) {
    return sFile ? sNumber : 0;
}

int DebugLog_Mark(void) {
    if (!sFile) return 0;
    Lock();
    int n = ++sMarks;
    PrintfLocked("--- MARK %d ---", n);
    FlushLocked();
    Unlock();
    return n;
}

void DebugLog_Flush(void) {
    Lock();
    FlushLocked();
    Unlock();
}

/* Every PERF_LINE_SECONDS: what the frames cost over the window, as deltas of
 * the counters the quick dump also reports. */
void DebugLog_Tick(void) {
    static uint64_t lastTick;
    static Platform3DSRuntimeStats last;
    static PortPpuGpu3DSStats lastGpu;
    if (!sFile) {
        lastTick = 0;
        return;
    }
    const uint64_t now = Platform3DS_SystemTick();
    const uint64_t tps = Platform3DS_TicksPerSecond();
    Platform3DSRuntimeStats rt;
    PortPpuGpu3DSStats gpu;
    if (lastTick == 0) {
        lastTick = now;
        Platform3DS_GetRuntimeStats(&last);
        PortPpuGpu3DS_GetStats(&lastGpu);
        return;
    }
    if (now - lastTick < PERF_LINE_SECONDS * tps) return;
    Platform3DS_GetRuntimeStats(&rt);
    PortPpuGpu3DS_GetStats(&gpu);
    SecondScreenSnapshot snap;
    Port_SecondScreenState_Read(&snap);

    const double secs = (double)(now - lastTick) / (double)tps;
    const uint64_t logic = rt.logicFrames - last.logicFrames;
    const uint64_t shown = rt.presentedFrames - last.presentedFrames;
    const double msPerTick = 1000.0 / (double)tps;
    const double work = logic ? (double)(rt.engineWorkTicks - last.engineWorkTicks) * msPerTick / (double)logic : 0.0;
    const double vbl = shown ? (double)(rt.vblankWaitTicks - last.vblankWaitTicks) * msPerTick / (double)shown : 0.0;
    const uint64_t gpuShown = gpu.renderedFrames - lastGpu.renderedFrames;
    const uint64_t gpuFallback = gpu.fallbackFrames - lastGpu.fallbackFrames;
    DebugLog_Printf("perf: shown %.1f fps, logic %.1f/s, skipped %llu, work %.2f ms/logic (max %.2f), "
                    "vblank wait %.2f ms/frame, gpu frames %llu fallback %llu decodes %llu, area %02X room %02X, "
                    "app free %lu KB",
                    (double)shown / secs, (double)logic / secs,
                    (unsigned long long)(rt.old3dsSkippedPresentations - last.old3dsSkippedPresentations), work,
                    (double)rt.engineWorkMaxTicks * msPerTick, vbl, (unsigned long long)gpuShown,
                    (unsigned long long)gpuFallback, (unsigned long long)(gpu.tileDecodes - lastGpu.tileDecodes),
                    snap.area, snap.room, (unsigned long)(rt.applicationMemoryFree / 1024));
#ifdef PPU_GPU3DS_PROFILE
    /* Where the GPU command build goes, per GPU frame. sig and retain are
     * inside maps; decode is inside whichever phase reached it. */
    static double lastPhase[PPU_GPU3DS_PHASE_COUNT];
    if (gpuShown) {
        double ms[PPU_GPU3DS_PHASE_COUNT];
        for (int i = 0; i < PPU_GPU3DS_PHASE_COUNT; ++i)
            ms[i] = (gPpuGpu3DSPhase[i] - lastPhase[i]) * msPerTick / (double)gpuShown;
        DebugLog_Printf("build: bands %.2f merge %.2f maps %.2f (sig %.2f retain %.2f) scene %.2f objwin %.2f "
                        "regions %.2f bg %.2f obj %.2f decode %.2f ms/frame",
                        ms[PPU_GPU3DS_PHASE_BANDS], ms[PPU_GPU3DS_PHASE_MERGE], ms[PPU_GPU3DS_PHASE_MAPS],
                        ms[PPU_GPU3DS_PHASE_MAPSIG], ms[PPU_GPU3DS_PHASE_MAPRETAIN], ms[PPU_GPU3DS_PHASE_SCENE],
                        ms[PPU_GPU3DS_PHASE_OBJWIN], ms[PPU_GPU3DS_PHASE_REGIONS], ms[PPU_GPU3DS_PHASE_BG],
                        ms[PPU_GPU3DS_PHASE_OBJ], ms[PPU_GPU3DS_PHASE_DECODE]);
    }
    memcpy(lastPhase, gPpuGpu3DSPhase, sizeof(lastPhase));
#endif
    lastTick = now;
    last = rt;
    lastGpu = gpu;
}
