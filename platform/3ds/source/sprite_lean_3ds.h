#ifndef SPRITE_LEAN_3DS_H
#define SPRITE_LEAN_3DS_H

/* The stereo 3D depth of each tile of a sprite that leans (docs/stereo-3d.md, "Lean per tile row" and "Sprite depth
 * corrections"). An OAM entry carries two depths, quarter pixels: virtuappu_mode1_obj_depth at its near edge and
 * virtuappu_mode1_obj_depth_top at its far edge. Normally the near edge is the bottom and the far one the top, and each
 * row of tiles takes the depth in between (a standing sprite, or one that hangs: PORT_DEPTH_INVERT only swaps which of the
 * two is higher). With SPRITE_LEAN_3DS_COLUMNS set in the top depth the lean runs across the columns instead: the near
 * edge is the right one and the far edge the left one (PORT_DEPTH_LEFT has the right edge at the ground, PORT_DEPTH_RIGHT
 * the left one). Both are in screen coordinates: a flipped sprite has its rows and columns the other way round.
 *
 * Tiles are drawn in runs of equal depth, a batch each, so a column lean is drawn column by column (SpriteLean3DS_TileAt)
 * and a row lean row by row. Header only, no dependencies: the GPU model (port_ppu_gpu_3ds_model.c) and its host test
 * (platform/3ds/tests/sprite_lean_3ds_test.c) include it. */

#include <stdbool.h>
#include <stdint.h>

enum {
    SPRITE_LEAN_3DS_COLUMNS = 0x80u, /* bit 7 of the top depth: lean across the columns (right edge to left edge) */
    SPRITE_LEAN_3DS_MAX_RUNS = 20,   /* more changes of depth than this in one sprite: one depth, the highest */
    SPRITE_LEAN_3DS_MAX_TILES = 64,  /* a bigger sprite (only affine double-size ones) does not lean */
    SPRITE_LEAN_3DS_MAX_DEPTH = 20,  /* never past the HUD's 6 px (24 quarters): the HUD is always in front */
    SPRITE_LEAN_3DS_TILE = 8
};

typedef struct SpriteLean3DS {
    /* The entry as the GPU model sees it (PpuGpu3DSObj): position and bounds on the screen, size in the tile data. */
    int x, y, width, height, boundsWidth, boundsHeight;
    bool affine, hflip, vflip;
    int16_t pa, pb, pc, pd;
    unsigned depthNear; /* at the bottom edge, or the right edge with `columns` */
    unsigned depthFar;  /* at the top edge, or the left edge with `columns` */
    bool columns;
    unsigned support;   /* what the layer under it adds to every tile */
} SpriteLean3DS;

/* The tile drawn at `step` (0 .. rows * cols - 1): row by row, or column by column for a column lean, so that each run of
 * equal depth is one stretch of the drawing order. The tiles of one sprite never overlap, so their order changes nothing
 * on the screen. */
static inline void SpriteLean3DS_TileAt(int rows, int cols, bool columns, int step, int* row, int* col) {
    if (columns) {
        *row = step % rows;
        *col = step / rows;
    } else {
        *row = step / cols;
        *col = step % cols;
    }
}

/* The depth of every tile, stored row by row (tileDepth[row * cols + col], rows and columns of the tile data); returns how
 * many runs of equal depth the drawing order makes (1 once flattened to the highest). 0 when the sprite has more than
 * SPRITE_LEAN_3DS_MAX_TILES tiles or a singular matrix: the caller draws it with one depth. */
static inline int SpriteLean3DS_Fill(const SpriteLean3DS* s, uint8_t* tileDepth) {
    const int rows = s->height / SPRITE_LEAN_3DS_TILE, cols = s->width / SPRITE_LEAN_3DS_TILE;
    if (rows <= 0 || cols <= 0 || rows * cols > SPRITE_LEAN_3DS_MAX_TILES) return 0;
    const int64_t determinant = (int64_t)s->pa * s->pd - (int64_t)s->pb * s->pc;
    if (s->affine && determinant == 0) return 0;
    const bool leans = s->depthFar != s->depthNear;
    unsigned tallest = 0;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            /* The tile's place on the screen (a flipped sprite has its rows and columns the other way round). */
            const int screenRow = s->vflip ? rows - 1 - row : row;
            const int screenCol = s->hflip ? cols - 1 - col : col;
            int depthI = (int)s->depthNear;
            if (leans) {
                /* How far the middle of the tile is from the near edge, and the whole extent along the lean. */
                unsigned fromNear = s->columns ? (unsigned)(cols - 1 - screenCol) * SPRITE_LEAN_3DS_TILE + SPRITE_LEAN_3DS_TILE / 2u
                                               : (unsigned)(rows - 1 - screenRow) * SPRITE_LEAN_3DS_TILE + SPRITE_LEAN_3DS_TILE / 2u;
                unsigned span = (unsigned)(s->columns ? cols : rows) * SPRITE_LEAN_3DS_TILE;
                if (s->affine) {
                    /* Rotated or scaled: where the middle of the tile lands on the screen (the inverse of the OBJ's matrix,
                     * the same mapping as the GPU model's geometry), measured from the bounds' bottom or right edge. */
                    const int sx = (col * SPRITE_LEAN_3DS_TILE + SPRITE_LEAN_3DS_TILE / 2 - s->width / 2) * 512 - 1;
                    const int sy = (row * SPRITE_LEAN_3DS_TILE + SPRITE_LEAN_3DS_TILE / 2 - s->height / 2) * 512 - 1;
                    float from;
                    if (s->columns) {
                        const float centerX = s->x + s->boundsWidth * 0.5f + 0.5f;
                        const float px = centerX + (float)((int64_t)s->pd * sx - (int64_t)s->pb * sy) / (2.0f * (float)determinant);
                        from = (float)(s->x + s->boundsWidth) - px;
                        span = s->boundsWidth > 0 ? (unsigned)s->boundsWidth : span;
                    } else {
                        const float centerY = s->y + s->boundsHeight * 0.5f + 0.5f;
                        const float py = centerY + (float)(-(int64_t)s->pc * sx + (int64_t)s->pa * sy) / (2.0f * (float)determinant);
                        from = (float)(s->y + s->boundsHeight) - py;
                        span = s->boundsHeight > 0 ? (unsigned)s->boundsHeight : span;
                    }
                    if (from < 0.0f) from = 0.0f;
                    fromNear = from > (float)span ? span : (unsigned)from;
                }
                depthI += ((int)s->depthFar - (int)s->depthNear) * (int)fromNear / (int)span;
            }
            unsigned depth = (unsigned)(depthI < 0 ? 0 : depthI) + s->support;
            if (depth > SPRITE_LEAN_3DS_MAX_DEPTH) depth = SPRITE_LEAN_3DS_MAX_DEPTH;
            tileDepth[row * cols + col] = (uint8_t)depth;
            if (depth > tallest) tallest = depth;
        }
    }
    /* Runs in the drawing order. Too many changes of depth for one sprite (each is a batch): one depth, the highest. */
    int runs = 0, row = 0, col = 0;
    unsigned last = 0;
    for (int step = 0; step < rows * cols; ++step) {
        SpriteLean3DS_TileAt(rows, cols, s->columns, step, &row, &col);
        const unsigned depth = tileDepth[row * cols + col];
        if (step == 0 || depth != last) ++runs;
        last = depth;
    }
    if (runs > SPRITE_LEAN_3DS_MAX_RUNS) {
        for (int tile = 0; tile < rows * cols; ++tile) tileDepth[tile] = (uint8_t)tallest;
        runs = 1;
    }
    return runs;
}

#endif
