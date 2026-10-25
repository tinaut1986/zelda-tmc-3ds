#include "scene_pictures_3ds.h"

#include "app_paths_3ds.h"
#include "cpu/mode1.h"
#include "fade_gpu_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

extern bool Port_Config_GetColorCorrection(void);
extern bool Port_Config_GetWideAi(void); /* these pictures are generated art: only WIDE + AI shows them */

#define TITLE_SD TMC3DS_APP_DIR "/title-forest.bin"
#define TITLE_ROMFS "romfs:/wide-scenes/title-forest.bin"
#define CAVE_SD TMC3DS_APP_DIR "/fileselect-cave.bin"
#define CAVE_ROMFS "romfs:/wide-scenes/fileselect-cave.bin"

/* The GBA BIOS's LZ77 (LZ77UnCompWram): a header of 0x10 and the 24-bit size, then groups of a flag byte and eight
 * items, a literal byte or (bit set) a copy of 3-18 bytes from up to 4096 back. */
static bool Lz77Decode(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) {
    if (inSize < 4 || in[0] != 0x10 || (size_t)(in[1] | in[2] << 8 | in[3] << 16) != outSize) return false;
    size_t i = 4, o = 0;
    while (o < outSize) {
        if (i >= inSize) return false;
        const uint8_t flags = in[i++];
        for (int bit = 0; bit < 8 && o < outSize; ++bit) {
            if (flags & (0x80 >> bit)) {
                if (i + 1 >= inSize) return false;
                const size_t len = (size_t)(in[i] >> 4) + 3, back = (size_t)((in[i] & 15) << 8 | in[i + 1]) + 1;
                i += 2;
                if (back > o) return false;
                for (size_t k = 0; k < len && o < outSize; ++k, ++o) out[o] = out[o - back];
            } else {
                if (i >= inSize) return false;
                out[o++] = in[i++];
            }
        }
    }
    return true;
}

bool ScenePictures_ReadFile(const char* sd, const char* romfs, void* out, size_t bytes) {
    FILE* f = sd ? fopen(sd, "rb") : NULL;
    if (f == NULL && romfs) f = fopen(romfs, "rb");
    if (f == NULL) return false;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long size = ftell(f);
        rewind(f);
        if (size == (long)bytes) {
            ok = fread(out, 1, bytes, f) == bytes;
        } else if (size > 4 && (size_t)size < bytes) {
            uint8_t* packed = malloc((size_t)size);
            if (packed != NULL && fread(packed, 1, (size_t)size, f) == (size_t)size) {
                if ((packed[0] & 0x0f) == 8 && ((packed[0] << 8) | packed[1]) % 31 == 0) {
                    /* A zlib stream (to_console.py's default). */
                    uLongf got = (uLongf)bytes;
                    ok = uncompress(out, &got, packed, (uLong)size) == Z_OK && got == bytes;
                } else {
                    ok = Lz77Decode(packed, (size_t)size, out, bytes);
                }
            }
            free(packed);
        }
    }
    fclose(f);
    return ok;
}

static int sAvailable = -1; /* -1 not looked, 0 none, 1 a file */
static uint16_t* sFaded555;  /* scratch for the software renderer's faded copy */
static uint32_t* sFadedAbgr;
static uint16_t* sTitle555;
static uint32_t* sTitleAbgr;
static bool sAbgrCorrected;

bool ScenePictures_TitleAvailable(void) {
    if (!Port_Config_GetWideAi()) return false;
    if (sAvailable < 0) sAvailable = access(TITLE_SD, F_OK) == 0 || access(TITLE_ROMFS, F_OK) == 0;
    return sAvailable != 0;
}

const uint16_t* ScenePictures_Title555(void) {
    if (sTitle555 != NULL || !ScenePictures_TitleAvailable()) return sTitle555;
    uint16_t* pixels = malloc(SCENE_TITLE_W * SCENE_TITLE_H * sizeof(uint16_t));
    if (pixels == NULL ||
        !ScenePictures_ReadFile(TITLE_SD, TITLE_ROMFS, pixels, SCENE_TITLE_W * SCENE_TITLE_H * sizeof(uint16_t))) {
        free(pixels);
        sAvailable = 0;
        return NULL;
    }
    /* The file is RGB565 (tools/scene-extend/to_console.py); back to the GBA's 15-bit colour. */
    for (unsigned i = 0; i < SCENE_TITLE_W * SCENE_TITLE_H; ++i) {
        const uint16_t c = pixels[i];
        pixels[i] = (uint16_t)(((c >> 11) & 31u) | ((((c >> 5) & 63u) >> 1) << 5) | ((c & 31u) << 10));
    }
    sTitle555 = pixels;
    return sTitle555;
}

const uint32_t* ScenePictures_TitleAbgr(void) {
    const uint16_t* gba = ScenePictures_Title555();
    if (gba == NULL) return NULL;
    const bool corrected = Port_Config_GetColorCorrection();
    if (sTitleAbgr == NULL) {
        sTitleAbgr = malloc(SCENE_TITLE_W * SCENE_TITLE_H * sizeof(uint32_t));
        if (sTitleAbgr == NULL) return NULL;
        sAbgrCorrected = !corrected;
    }
    if (sAbgrCorrected != corrected) {
        for (unsigned i = 0; i < SCENE_TITLE_W * SCENE_TITLE_H; ++i)
            sTitleAbgr[i] = gba[i] != 0 ? virtuappu_mode1_rgb555_to_abgr8888(gba[i]) : 0u;
        sAbgrCorrected = corrected;
    }
    return sTitleAbgr;
}

const uint32_t* ScenePictures_TitleAbgrFaded(void) {
    const uint16_t* gba = ScenePictures_Title555();
    if (gba == NULL) return NULL;
    const size_t count = SCENE_TITLE_W * SCENE_TITLE_H;
    if (sFaded555 == NULL) sFaded555 = malloc(count * sizeof(uint16_t));
    if (sFadedAbgr == NULL) sFadedAbgr = malloc(count * sizeof(uint32_t));
    if (sFaded555 == NULL || sFadedAbgr == NULL || !FadeGpu3DS_FadeShown(gba, sFaded555, count))
        return ScenePictures_TitleAbgr();
    for (size_t i = 0; i < count; ++i)
        sFadedAbgr[i] = sFaded555[i] != 0 ? virtuappu_mode1_rgb555_to_abgr8888(sFaded555[i]) : 0u;
    return sFadedAbgr;
}

void ScenePictures_Release(void) {
    free(sTitle555);
    free(sTitleAbgr);
    free(sFaded555);
    free(sFadedAbgr);
    sTitle555 = NULL;
    sTitleAbgr = NULL;
    sFaded555 = NULL;
    sFadedAbgr = NULL;
}

/* The pause menu's background extended by the game itself, from its own BG3 in VRAM: nothing of it is shipped. It is a
 * lattice of Ezlo caps, so the extension is worked out on the tile map (30x20 entries, cheap): the two shortest shifts
 * that repeat the map, then every entry outside the screen the one most of its lattice copies inside have, then the
 * caps the screen's edge cuts off the lattice completed from a whole copy (tools/scene-extend/extend_lattice.py is the
 * same in pixels, for trying a picture on the PC). */
enum { PAUSE_MAP_W = 30, PAUSE_MAP_H = 20, PAUSE_OUT_W = SCENE_PAUSE_W / 8, PAUSE_OUT_H = SCENE_PAUSE_H / 8,
       PAUSE_LEFT = 10, PAUSE_TOP = 5, PAUSE_REACH = 12, PAUSE_EDGE = 3 };
extern uint8_t gVram[0x18000];
static uint16_t sPauseMap[PAUSE_MAP_H][PAUSE_MAP_W]; /* the map the picture was made from */
static uint8_t* sPauseIndices;
static uint16_t* sPause555;
static unsigned sPauseFirstPalette;
static int sPauseLattice[2][2];

static uint16_t PauseVote(int px, int py, bool skipSelf) {
    /* The entry most of the lattice copies of (px, py) inside the map have (few distinct ones: a small list). */
    uint16_t entries[16];
    unsigned counts[16], used = 0;
    for (int i = -PAUSE_REACH; i <= PAUSE_REACH; ++i)
        for (int j = -PAUSE_REACH; j <= PAUSE_REACH; ++j) {
            const int qx = px + i * sPauseLattice[0][0] + j * sPauseLattice[1][0];
            const int qy = py + i * sPauseLattice[0][1] + j * sPauseLattice[1][1];
            if ((skipSelf && i == 0 && j == 0) || qx < 0 || qx >= PAUSE_MAP_W || qy < 0 || qy >= PAUSE_MAP_H) continue;
            const uint16_t e = sPauseMap[qy][qx];
            unsigned k = 0;
            while (k < used && entries[k] != e) ++k;
            if (k == used) {
                if (used == 16) continue;
                entries[used] = e;
                counts[used++] = 0;
            }
            counts[k]++;
        }
    unsigned best = 0;
    for (unsigned k = 1; k < used; ++k)
        if (counts[k] > counts[best]) best = k;
    return used ? entries[best] : 0;
}

static bool PauseFindLattice(void) {
    int found = 0;
    for (int len = 1; len <= PAUSE_MAP_W * PAUSE_MAP_W && found < 2; ++len) /* shortest first */
        for (int dy = 0; dy <= PAUSE_MAP_H / 2 && found < 2; ++dy)
            for (int dx = -PAUSE_MAP_W / 2; dx <= PAUSE_MAP_W / 2 && found < 2; ++dx) {
                if ((dy == 0 && dx <= 0) || dx * dx + dy * dy != len) continue;
                if (found == 1 && sPauseLattice[0][0] * dy - sPauseLattice[0][1] * dx == 0) continue;
                unsigned n = 0, same = 0;
                for (int y = 0; y + dy < PAUSE_MAP_H; ++y)
                    for (int x = dx < 0 ? -dx : 0; x < PAUSE_MAP_W && x + dx < PAUSE_MAP_W; ++x) {
                        ++n;
                        same += sPauseMap[y][x] == sPauseMap[y + dy][x + dx];
                    }
                if (n >= 150 && same * 100 >= n * 95) {
                    sPauseLattice[found][0] = dx;
                    sPauseLattice[found][1] = dy;
                    ++found;
                }
            }
    return found == 2;
}

static void PauseMake(void) {
    static uint16_t out[PAUSE_OUT_H][PAUSE_OUT_W];
    /* The commonest entry is the plain background. */
    uint16_t background = sPauseMap[0][0];
    {
        unsigned bestCount = 0;
        for (int i = 0; i < PAUSE_MAP_W * PAUSE_MAP_H; ++i) {
            const uint16_t e = sPauseMap[i / PAUSE_MAP_W][i % PAUSE_MAP_W];
            unsigned c = 0;
            for (int k = 0; k < PAUSE_MAP_W * PAUSE_MAP_H; ++k) c += sPauseMap[k / PAUSE_MAP_W][k % PAUSE_MAP_W] == e;
            if (c > bestCount) bestCount = c, background = e;
        }
    }
    for (int y = 0; y < PAUSE_OUT_H; ++y)
        for (int x = 0; x < PAUSE_OUT_W; ++x) {
            const int px = x - PAUSE_LEFT, py = y - PAUSE_TOP;
            out[y][x] = px >= 0 && px < PAUSE_MAP_W && py >= 0 && py < PAUSE_MAP_H ? 0 : PauseVote(px, py, false);
        }
    /* Caps the edge cuts off the lattice: runs of edge entries their lattice copies disagree with. */
    for (int side = 0; side < 2; ++side) {
        const int ex = side ? PAUSE_MAP_W - 1 : 0, c0 = side ? PAUSE_MAP_W - PAUSE_EDGE : 0;
        const int past0 = side ? PAUSE_MAP_W : -PAUSE_EDGE;
        for (int y0 = 0; y0 < PAUSE_MAP_H;) {
            int y1 = y0;
            while (y1 < PAUSE_MAP_H && sPauseMap[y1][ex] != background && sPauseMap[y1][ex] != PauseVote(ex, y1, true)) ++y1;
            if (y1 == y0) {
                ++y0;
                continue;
            }
            bool done = false;
            for (int sy = -y0; sy < PAUSE_MAP_H - y1 + 1 && !done; ++sy)
                for (int sx = -PAUSE_MAP_W; sx < PAUSE_MAP_W && !done; ++sx) {
                    if ((sx == 0 && sy == 0) || past0 + sx < 0 || past0 + PAUSE_EDGE + sx > PAUSE_MAP_W) continue;
                    bool match = true;
                    for (int y = y0; y < y1 && match; ++y)
                        for (int x = c0; x < c0 + PAUSE_EDGE && match; ++x)
                            if (sPauseMap[y][x] != background)
                                match = x + sx >= 0 && x + sx < PAUSE_MAP_W && sPauseMap[y + sy][x + sx] == sPauseMap[y][x];
                    if (!match) continue;
                    for (int y = y0; y < y1; ++y)
                        for (int x = past0; x < past0 + PAUSE_EDGE; ++x)
                            if (sPauseMap[y + sy][x + sx] != background)
                                out[y + PAUSE_TOP][x + PAUSE_LEFT] = sPauseMap[y + sy][x + sx];
                    done = true;
                }
            y0 = y1;
        }
    }
    /* Into pixels: palette indices of the 4bpp tiles (character base 2), relative to the first of four palettes. */
    unsigned lowest = 15, highest = 0;
    for (int y = 0; y < PAUSE_OUT_H; ++y)
        for (int x = 0; x < PAUSE_OUT_W; ++x)
            if (out[y][x]) {
                const unsigned bank = out[y][x] >> 12;
                if (bank < lowest) lowest = bank;
                if (bank > highest) highest = bank;
            }
    sPauseFirstPalette = lowest > 12 ? 12 : lowest;
    if (highest >= sPauseFirstPalette + 4) return; /* more palettes than the picture can hold */
    for (int y = 0; y < SCENE_PAUSE_H; ++y)
        for (int x = 0; x < SCENE_PAUSE_W; ++x) {
            const uint16_t e = out[y >> 3][x >> 3];
            uint8_t index = 0;
            if (e) {
                const unsigned r = (e & 0x800) ? 7u - (y & 7u) : (y & 7u), c = (e & 0x400) ? 7u - (x & 7u) : (x & 7u);
                const uint8_t b = gVram[0x8000u + (e & 0x3FFu) * 32u + r * 4u + c / 2u];
                const unsigned i = (c & 1u) ? b >> 4 : b & 15u;
                if (i) index = (uint8_t)(((e >> 12) - sPauseFirstPalette) * 16u + i);
            }
            sPauseIndices[y * SCENE_PAUSE_W + x] = index;
        }
}

const uint8_t* ScenePictures_PauseIndices(unsigned* firstPalette, bool* remade) {
    /* BG3's map (screen base 30) as the screen shows it; made again only when it changed. */
    bool same = sPauseIndices != NULL;
    for (int y = 0; y < PAUSE_MAP_H; ++y)
        for (int x = 0; x < PAUSE_MAP_W; ++x) {
            const uint16_t e = (uint16_t)(gVram[0xF000 + (y * 32 + x) * 2] | gVram[0xF000 + (y * 32 + x) * 2 + 1] << 8);
            if (e != sPauseMap[y][x]) same = false;
            sPauseMap[y][x] = e;
        }
    if (remade) *remade = !same;
    if (!same) {
        free(sPause555);
        sPause555 = NULL;
        if (sPauseIndices == NULL) sPauseIndices = malloc(SCENE_PAUSE_W * SCENE_PAUSE_H);
        if (sPauseIndices == NULL) return NULL;
        memset(sPauseIndices, 0, SCENE_PAUSE_W * SCENE_PAUSE_H);
        if (PauseFindLattice()) PauseMake();
    }
    *firstPalette = sPauseFirstPalette;
    return sPauseIndices;
}

const uint16_t* ScenePictures_Pause555(void) {
    unsigned first;
    const uint8_t* indices = ScenePictures_PauseIndices(&first, NULL);
    if (indices == NULL) return NULL;
    if (sPause555 == NULL && (sPause555 = malloc(SCENE_PAUSE_W * SCENE_PAUSE_H * sizeof(uint16_t))) != NULL) {
        const uint16_t* palettes = FadeGpu3DS_ShownPalettes() + first * 16u;
        for (int i = 0; i < SCENE_PAUSE_W * SCENE_PAUSE_H; ++i) sPause555[i] = indices[i] ? palettes[indices[i]] : 0;
    }
    return sPause555;
}

void ScenePictures_ReleasePause(void) {
    free(sPauseIndices);
    free(sPause555);
    sPauseIndices = NULL;
    sPause555 = NULL;
}

static int sCaveAvailable = -1; /* as sAvailable */
static uint8_t* sCave;

bool ScenePictures_CaveAvailable(void) {
    if (!Port_Config_GetWideAi()) return false;
    if (sCaveAvailable < 0) sCaveAvailable = access(CAVE_SD, F_OK) == 0 || access(CAVE_ROMFS, F_OK) == 0;
    return sCaveAvailable != 0;
}

const uint8_t* ScenePictures_CaveIndices(void) {
    if (sCave != NULL || !ScenePictures_CaveAvailable()) return sCave;
    uint8_t* indices = malloc(SCENE_CAVE_W * SCENE_CAVE_H);
    if (indices == NULL || !ScenePictures_ReadFile(CAVE_SD, CAVE_ROMFS, indices, SCENE_CAVE_W * SCENE_CAVE_H)) {
        free(indices);
        sCaveAvailable = 0;
        return NULL;
    }
    sCave = indices;
    return sCave;
}

void ScenePictures_ReleaseCave(void) {
    free(sCave);
    sCave = NULL;
}

static const char* const kShaftFiles[SCENE_SHAFT_LAYERS] = { "crenel-paths-ground.bin", "crenel-paths-ledges.bin",
                                                               "crenel-paths-shadows.bin" };
static const unsigned kShaftHeights[SCENE_SHAFT_LAYERS] = { SCENE_SHAFT_GROUND_H, SCENE_SHAFT_LAYER_H,
                                                            SCENE_SHAFT_LAYER_H };
static int8_t sShaftAvailable[SCENE_SHAFT_LAYERS] = { -1, -1, -1 }; /* as sAvailable */
static uint16_t* sShaft555[SCENE_SHAFT_LAYERS];
static int sShaftBg = -1, sShaftX, sShaftY[SCENE_SHAFT_LAYERS];

const uint16_t* ScenePictures_Shaft555(SceneShaftLayer layer, unsigned* height) {
    if (layer >= SCENE_SHAFT_LAYERS) return NULL;
    if (height) *height = kShaftHeights[layer];
    if (!Port_Config_GetWideAi()) return NULL; /* generated art: only WIDE + AI, checked every frame */
    if (sShaft555[layer] != NULL || sShaftAvailable[layer] == 0) return sShaft555[layer];
    char sd[160], romfs[96];
    snprintf(sd, sizeof(sd), "%s/%s", TMC3DS_APP_DIR, kShaftFiles[layer]);
    snprintf(romfs, sizeof(romfs), "romfs:/wide-scenes/%s", kShaftFiles[layer]);
    const size_t count = (size_t)2 * SCENE_SHAFT_STRIP_W * kShaftHeights[layer];
    uint16_t* pixels = malloc(count * sizeof(uint16_t));
    if (pixels == NULL || !ScenePictures_ReadFile(sd, romfs, pixels, count * sizeof(uint16_t))) {
        free(pixels);
        sShaftAvailable[layer] = 0;
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) { /* RGB565 to the GBA's 15-bit colour, as the title's */
        const uint16_t c = pixels[i];
        pixels[i] = (uint16_t)(((c >> 11) & 31u) | ((((c >> 5) & 63u) >> 1) << 5) | ((c & 31u) << 10));
    }
    sShaftAvailable[layer] = 1;
    sShaft555[layer] = pixels;
    return pixels;
}

void ScenePictures_ReleaseShaft(void) {
    for (unsigned i = 0; i < SCENE_SHAFT_LAYERS; ++i) {
        free(sShaft555[i]);
        sShaft555[i] = NULL;
    }
}

void ScenePictures_SetShaftView(int groundBg, int x, int groundY, int ledgesY, int shadowsY) {
    sShaftBg = groundBg;
    sShaftX = x;
    sShaftY[SCENE_SHAFT_GROUND] = groundY;
    sShaftY[SCENE_SHAFT_LEDGES] = ledgesY;
    sShaftY[SCENE_SHAFT_SHADOWS] = shadowsY;
}

int ScenePictures_ShaftView(int* x, int y[SCENE_SHAFT_LAYERS]) {
    if (x) *x = sShaftX;
    if (y)
        for (unsigned i = 0; i < SCENE_SHAFT_LAYERS; ++i) y[i] = sShaftY[i];
    return sShaftBg;
}
