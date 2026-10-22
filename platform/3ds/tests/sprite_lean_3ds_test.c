/* Host test of the per-tile stereo depth of a leaning sprite (platform/3ds/source/sprite_lean_3ds.h): rows (the normal
 * rule and PORT_DEPTH_DOWN), columns (PORT_DEPTH_LEFT / PORT_DEPTH_RIGHT), flips, flat, 1xN and Nx1 sprites, the run count
 * in drawing order and the run cap.
 *
 *   cc -std=c11 -Wall -Wextra -Iplatform/3ds/source platform/3ds/tests/sprite_lean_3ds_test.c -o /tmp/sprite_lean_3ds_test
 *   /tmp/sprite_lean_3ds_test
 */
#include "sprite_lean_3ds.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);        \
            return 1;                                                      \
        }                                                                  \
    } while (0)

/* A plain (not affine) sprite of cols x rows tiles. */
static SpriteLean3DS plain(int cols, int rows, unsigned nearQ, unsigned farQ, bool columns) {
    SpriteLean3DS s;
    memset(&s, 0, sizeof(s));
    s.x = 40;
    s.y = 30;
    s.width = s.boundsWidth = cols * 8;
    s.height = s.boundsHeight = rows * 8;
    s.depthNear = nearQ;
    s.depthFar = farQ;
    s.columns = columns;
    return s;
}

int main(void) {
    uint8_t d[SPRITE_LEAN_3DS_MAX_TILES];

    /* Every tile once, in either order. */
    for (int columns = 0; columns < 2; ++columns) {
        int seen[6 * 4] = {0};
        for (int step = 0; step < 6 * 4; ++step) {
            int row, col;
            SpriteLean3DS_TileAt(6, 4, columns != 0, step, &row, &col);
            CHECK(row >= 0 && row < 6 && col >= 0 && col < 4);
            ++seen[row * 4 + col];
        }
        for (int i = 0; i < 6 * 4; ++i) CHECK(seen[i] == 1);
    }
    {
        int row, col;
        SpriteLean3DS_TileAt(6, 4, true, 7, &row, &col); /* column by column: the 8th tile is column 1, row 1 */
        CHECK(row == 1 && col == 1);
        SpriteLean3DS_TileAt(6, 4, false, 7, &row, &col); /* row by row: row 1, column 3 */
        CHECK(row == 1 && col == 3);
    }

    /* Up (the normal rule): 2 x 4 tiles, 0 at the bottom edge, 8 at the top; each row the depth at its middle. */
    {
        const SpriteLean3DS s = plain(2, 4, 0, 8, false);
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {7, 5, 3, 1}; /* top row first */
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 2; ++col) CHECK(d[row * 2 + col] == want[row]);
    }
    /* Down (PORT_DEPTH_DOWN / INVERT): the bottom edge carries the higher depth. */
    {
        const SpriteLean3DS s = plain(2, 4, 8, 0, false);
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {1, 3, 5, 7};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 2; ++col) CHECK(d[row * 2 + col] == want[row]);
    }
    /* Up, flipped vertically: the tile data's top row is drawn at the bottom. */
    {
        SpriteLean3DS s = plain(2, 4, 0, 8, false);
        s.vflip = true;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {1, 3, 5, 7};
        for (int row = 0; row < 4; ++row) CHECK(d[row * 2] == want[row] && d[row * 2 + 1] == want[row]);
    }

    /* Left (PORT_DEPTH_LEFT): 4 x 2 tiles, the right edge at 0 (the ground), the left one at 8. One run per column:
     * drawn row by row it would make 8. */
    {
        const SpriteLean3DS s = plain(4, 2, 0, 8, true);
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {7, 5, 3, 1}; /* leftmost column first */
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col) CHECK(d[row * 4 + col] == want[col]);
    }
    /* Right (PORT_DEPTH_RIGHT): the left edge at 0, the right one at 8. */
    {
        const SpriteLean3DS s = plain(4, 2, 8, 0, true);
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {1, 3, 5, 7};
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col) CHECK(d[row * 4 + col] == want[col]);
    }
    /* Left, flipped horizontally: the screen keeps the depth rising to the left, so the tile data's column 0 (drawn at
     * the right) is the lowest. */
    {
        SpriteLean3DS s = plain(4, 2, 0, 8, true);
        s.hflip = true;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        const uint8_t want[4] = {1, 3, 5, 7};
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 4; ++col) CHECK(d[row * 4 + col] == want[col]);
    }
    /* A vertical flip does not change a column lean; a horizontal one does not change a row lean. */
    {
        SpriteLean3DS s = plain(4, 2, 0, 8, true);
        s.vflip = true;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        CHECK(d[0] == 7 && d[3] == 1 && d[4] == 7 && d[7] == 1);
        s = plain(2, 4, 0, 8, false);
        s.hflip = true;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        CHECK(d[0] == 7 && d[1] == 7 && d[6] == 1 && d[7] == 1);
    }

    /* Flat: one depth, one run, whichever axis. */
    for (int columns = 0; columns < 2; ++columns) {
        const SpriteLean3DS s = plain(4, 4, 5, 5, columns != 0);
        CHECK(SpriteLean3DS_Fill(&s, d) == 1);
        for (int i = 0; i < 16; ++i) CHECK(d[i] == 5);
    }

    /* 1 x N and N x 1. A strip one tile wide leaning across its columns takes the middle depth: one run. */
    {
        SpriteLean3DS s = plain(1, 8, 0, 8, true); /* 8 px wide, 64 tall, PORT_DEPTH_LEFT */
        CHECK(SpriteLean3DS_Fill(&s, d) == 1);
        for (int i = 0; i < 8; ++i) CHECK(d[i] == 4);
        s = plain(8, 1, 0, 8, true); /* 64 px wide, 8 tall, PORT_DEPTH_LEFT: 0..7 from the right */
        CHECK(SpriteLean3DS_Fill(&s, d) == 8);
        for (int col = 0; col < 8; ++col) CHECK(d[col] == (uint8_t)(8 * ((7 - col) * 8 + 4) / 64));
        s = plain(8, 1, 0, 8, false); /* the same strip with the normal rule: one row, the middle depth */
        CHECK(SpriteLean3DS_Fill(&s, d) == 1);
        for (int col = 0; col < 8; ++col) CHECK(d[col] == 4);
        s = plain(1, 8, 0, 8, false); /* the tall strip, normal rule */
        CHECK(SpriteLean3DS_Fill(&s, d) == 8);
        for (int row = 0; row < 8; ++row) CHECK(d[row] == (uint8_t)(8 * ((7 - row) * 8 + 4) / 64));
    }

    /* The widest lean (8 x 8 tiles, 0 to 20): 8 runs column by column, under the cap; drawn row by row it would be 64. */
    {
        const SpriteLean3DS s = plain(8, 8, 0, 20, true);
        CHECK(SpriteLean3DS_Fill(&s, d) == 8);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) CHECK(d[row * 8 + col] == d[col]);
        CHECK(d[0] > d[7]);
    }

    /* Support is added to every tile; the sum stops at 20 quarters. */
    {
        SpriteLean3DS s = plain(4, 1, 0, 12, true);
        s.support = 12;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        CHECK(d[0] == 20 && d[1] == 19 && d[2] == 16 && d[3] == 13); /* 10, 7, 4, 1 from the lean */
    }

    /* Affine, identity matrix: the column lean follows the screen, highest on the left, one run per column. */
    {
        SpriteLean3DS s = plain(4, 2, 0, 8, true);
        s.affine = true;
        s.pa = s.pd = 256;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        CHECK(d[0] > d[1] && d[1] > d[2] && d[2] > d[3]);
        for (int col = 0; col < 4; ++col) CHECK(d[4 + col] == d[col]);
        /* Mirrored by the matrix (pa = -1): the source's column 0 lands on the right, so it is the lowest. */
        s.pa = -256;
        CHECK(SpriteLean3DS_Fill(&s, d) == 4);
        CHECK(d[0] < d[1] && d[1] < d[2] && d[2] < d[3]);
        /* Rotated 90 degrees: the source's rows are the screen's columns, so a column lean runs along the rows of the
         * tile data (the same depth across each row). */
        s = plain(2, 4, 0, 8, true);
        s.affine = true;
        s.width = s.height = s.boundsWidth = s.boundsHeight = 32; /* 4 x 4 tiles, square so it rotates in place */
        s.pa = 0;
        s.pb = -256;
        s.pc = 256;
        s.pd = 0;
        const int runs = SpriteLean3DS_Fill(&s, d);
        CHECK(runs >= 4 && runs <= SPRITE_LEAN_3DS_MAX_RUNS);
        for (int row = 0; row < 4; ++row)
            for (int col = 1; col < 4; ++col) CHECK(d[row * 4 + col] == d[row * 4]);
        CHECK(d[0] != d[12]);
        /* A singular matrix: nothing (the caller draws it with one depth). */
        s.pa = s.pb = s.pc = s.pd = 0;
        CHECK(SpriteLean3DS_Fill(&s, d) == 0);
    }

    /* Rotated 45 degrees and leaning across 8 x 8 tiles: more changes of depth than the cap, so one depth, the highest. */
    {
        SpriteLean3DS s = plain(8, 8, 0, 20, true);
        s.affine = true;
        s.pa = 181;
        s.pb = -181;
        s.pc = 181;
        s.pd = 181;
        CHECK(SpriteLean3DS_Fill(&s, d) == 1);
        for (int i = 1; i < 64; ++i) CHECK(d[i] == d[0]);
        CHECK(d[0] > 0);
    }

    /* Too many tiles: no lean. */
    {
        const SpriteLean3DS s = plain(16, 8, 0, 8, true);
        CHECK(SpriteLean3DS_Fill(&s, d) == 0);
    }

    puts("sprite_lean_3ds_test: PASS");
    return 0;
}
