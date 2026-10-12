#pragma once
/* Frame-time recorder for debug-tools builds (docs/debug-tools.md, "Perf
 * recorder"). One row per presented frame into a RAM buffer while it runs;
 * nothing touches the SD card until it is stopped, when the rows are written
 * as debug/tmc-perf-NNNN.csv (tools/perf-csv/analyze.py summarises it). */
#include <stdbool.h>
#include <stdint.h>

typedef struct DebugPerfFrame {
    uint64_t startTick;      /* Port_PPU_PresentFrame entry */
    uint64_t workTicks;      /* the engine loop iteration (Platform3DS engineWorkLastTicks) */
    uint64_t vblankTicks;    /* the last vblank wait */
    uint64_t beginTicks;     /* GPU path: waiting for the previous GPU frame (C3D_FrameBegin) */
    uint64_t preflightTicks; /* GPU path: building the frame's commands and uploads */
    uint64_t renderTicks;    /* software PPU render (0 when the GPU drew it) */
    uint64_t topTicks;       /* the whole top-screen part of the present */
    uint64_t bottomTicks;    /* the bottom-screen paint that finished this frame, 0 if none */
    uint32_t logicFrames;    /* logic ticks since the previous row (more than 1 = presentations skipped) */
    uint8_t gpu;             /* 1: the GPU renderer drew this frame */
    /* GPU path, this frame only: what the builder did (deltas of the renderer's counters). */
    uint16_t decodes;        /* tiles decoded into the atlas */
    uint8_t mapRebuilds;     /* BG layers whose quads were re-emitted */
    uint8_t mapRefreshes;    /* BG layers kept, tiles re-decoded in place */
    uint8_t mapRebuildWhy[5]; /* PpuGpu3DSMapRebuild reasons: new, tilemap, palette, tiles, window */
    uint32_t vertices;       /* vertices emitted for the frame */
    uint32_t phaseTicks[8];  /* maps, mapsig, mapretain, bg, obj, decode, ring digest, ring cells (PPU_GPU3DS_PROFILE builds) */
    uint8_t ringWhy[4];      /* per BG: why its view ring was not used this frame (0: it was) */
    uint16_t ringCells;      /* ring cells brought up to date this frame (decoded or copied) */
    uint16_t ringMemoHits;   /* of those, copied from a cell already decoded this frame */
    uint8_t area, room;
} DebugPerfFrame;

/* Allocates the buffer; false when there is no memory for it. */
bool DebugPerf_Start(void);
/* Writes the CSV and frees the buffer. Returns the file number, or -1. */
int DebugPerf_Stop(void);
bool DebugPerf_Running(void);
unsigned DebugPerf_Rows(void);
unsigned DebugPerf_Capacity(void);
void DebugPerf_Record(const DebugPerfFrame* frame);
