#include "debug_perf_3ds.h"

#include "debug_files_3ds.h"
#include "debug_log_3ds.h"
#include "platform_3ds.h"
#include "tmc3ds_version.h"

#include <stdio.h>
#include <stdlib.h>

/* 36000 rows: ten minutes at 60 fps, ~2 MB. */
enum { PERF_CAPACITY = 36000 };

static DebugPerfFrame* sRows;
static unsigned sCount;
static volatile bool sRunning;

bool DebugPerf_Start(void) {
    if (sRunning) return true;
    sRows = (DebugPerfFrame*)malloc(sizeof(DebugPerfFrame) * PERF_CAPACITY);
    if (!sRows) return false;
    sCount = 0;
    sRunning = true;
    DebugLog_Printf("perf recorder: started");
    return true;
}

bool DebugPerf_Running(void) {
    return sRunning;
}

unsigned DebugPerf_Rows(void) {
    return sCount;
}

unsigned DebugPerf_Capacity(void) {
    return PERF_CAPACITY;
}

void DebugPerf_Record(const DebugPerfFrame* frame) {
    if (!sRunning || sCount >= PERF_CAPACITY) return;
    sRows[sCount++] = *frame;
}

int DebugPerf_Stop(void) {
    if (!sRunning) return -1;
    sRunning = false;
    char path[96];
    int n = DebugFiles_Next("perf", ".csv", path, sizeof(path));
    FILE* f = n > 0 ? fopen(path, "w") : NULL;
    if (f) {
        const double ms = 1000.0 / (double)Platform3DS_TicksPerSecond();
        fprintf(f, "# Minish Cap 3DS v%s (upstream %s), %s\n", TMC_PORT_VERSION_LABEL, TMC_UPSTREAM_VERSION,
                Platform3DS_IsNew3DS() ? "New 3DS" : "Old 3DS");
        fprintf(f, "frame,t_ms,interval_ms,work_ms,vblank_ms,gpu_begin_ms,gpu_build_ms,sw_render_ms,top_ms,"
                   "bottom_ms,logic_frames,gpu,area,room\n");
        const uint64_t t0 = sCount ? sRows[0].startTick : 0;
        for (unsigned i = 0; i < sCount; i++) {
            const DebugPerfFrame* r = &sRows[i];
            const double interval = i ? (double)(r->startTick - sRows[i - 1].startTick) * ms : 0.0;
            fprintf(f, "%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%lu,%u,%02X,%02X\n", i,
                    (double)(r->startTick - t0) * ms, interval, (double)r->workTicks * ms,
                    (double)r->vblankTicks * ms, (double)r->beginTicks * ms, (double)r->preflightTicks * ms,
                    (double)r->renderTicks * ms, (double)r->topTicks * ms, (double)r->bottomTicks * ms,
                    (unsigned long)r->logicFrames, r->gpu, r->area, r->room);
        }
        fclose(f);
        DebugLog_Printf("perf recorder: %u rows -> %s", sCount, path);
    } else {
        n = -1;
    }
    free(sRows);
    sRows = NULL;
    sCount = 0;
    return n;
}
