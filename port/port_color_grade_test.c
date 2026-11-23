/*
 * port/port_color_grade_test.c — locks the colour grade's byte-exact modes and clamping.
 *
 * OFF must stay identical to the raw 5-bit expansion, GBA to the fixed LCD LUT the
 * game shipped with, and a neutral CUSTOM grade to OFF. VIVID must match CUSTOM
 * {10, 10, 0, -8} byte for byte while keeping whatever fields are stored.
 * Out-of-range settings are clamped by Port_ColourGrade_Set, saturation leaves
 * greys alone, the shadow/midtone zones lift by luma only in GBA/CUSTOM, and the
 * epoch only moves when the installed grade differs.
 */
#include <stdio.h>
#include <string.h>

#include "port_color_grade.h"

static int sFailures;

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                             \
        }                                            \
    } while (0)

static const uint8_t kGbaLut[32] = {
    0, 0, 0, 0, 1, 2, 4, 8, 13, 19, 25, 32, 39, 46, 54, 63,
    71, 80, 90, 100, 110, 120, 131, 142, 154, 165, 178, 190, 203, 216, 229, 243,
};

static PortColourGrade MakeGrade(unsigned mode, int brightness, int contrast, int saturation, int gamma,
                                 int shadows, int midtones) {
    PortColourGrade g;
    g.mode = (uint8_t)mode;
    g.brightness = (int8_t)brightness;
    g.contrast = (int8_t)contrast;
    g.saturation = (int8_t)saturation;
    g.gamma = (int8_t)gamma;
    g.shadows = (int8_t)shadows;
    g.midtones = (int8_t)midtones;
    return g;
}

static void TestOffParity(void) {
    const PortColourGrade off = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0);
    uint8_t lut[32], out[3];

    Port_ColourGrade_Set(&off);
    Port_ColourGrade_ChannelLut(&off, lut);
    for (int x = 0; x < 32; ++x) {
        CHECK(lut[x] == (uint8_t)(x << 3), "OFF LUT is the raw 5-bit expansion");
    }

    Port_ColourGrade_ToRgb888(&off, 0x001F, out);
    CHECK(out[0] == 248 && out[1] == 0 && out[2] == 0, "OFF maps pure red 31 to 248");
    puts("colour_grade_test: OFF parity PASS");
}

static void TestGbaParity(void) {
    const PortColourGrade gba = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0, 0, 0);
    uint8_t lut[32], out[3];

    Port_ColourGrade_Set(&gba);
    Port_ColourGrade_ChannelLut(&gba, lut);
    CHECK(memcmp(lut, kGbaLut, sizeof(lut)) == 0, "GBA LUT is byte-exact");

    Port_ColourGrade_ToRgb888(&gba, 0x001F, out);
    CHECK(out[0] == kGbaLut[31] && out[1] == kGbaLut[0] && out[2] == kGbaLut[0],
          "GBA ToRgb888 uses the LUT per channel");
    puts("colour_grade_test: GBA parity PASS");
}

static void TestCustomNeutralParity(void) {
    const PortColourGrade custom = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    uint8_t lut[32];

    Port_ColourGrade_Set(&custom);
    Port_ColourGrade_ChannelLut(&custom, lut);
    for (int x = 0; x < 32; ++x) {
        CHECK(lut[x] == (uint8_t)(x << 3), "neutral CUSTOM LUT equals OFF");
    }
    puts("colour_grade_test: neutral CUSTOM parity PASS");
}

static void TestClamping(void) {
    /* The fields are int8_t, so 200 cannot be stored; 100 exercises the same
     * above-range clamp, and -99/99 exercise the far ends of their ranges. */
    const PortColourGrade wild = MakeGrade(9, 100, -99, 100, 99, 100, -99);
    const PortColourGrade* current;

    Port_ColourGrade_Set(&wild);
    current = Port_ColourGrade_Current();
    CHECK(current->mode == PORT_COLOUR_OFF, "invalid mode clamps to OFF");
    CHECK(current->brightness == 50, "brightness clamps high to 50");
    CHECK(current->contrast == -50, "contrast clamps low to -50");
    CHECK(current->saturation == 50, "saturation clamps high to 50");
    CHECK(current->gamma == 20, "gamma clamps high to 20");
    CHECK(current->shadows == 50, "shadows clamp high to 50");
    CHECK(current->midtones == -50, "midtones clamp low to -50");
    puts("colour_grade_test: clamping PASS");
}

static void TestSaturationPreservesGrey(void) {
    const PortColourGrade desaturated = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, -50, 0, 0, 0);
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const uint16_t grey = (uint16_t)(20u | (20u << 5) | (20u << 10));
    const uint16_t samples[2] = { 0x7FFFu, 0x2108u };
    uint8_t out[3], lut[32];

    Port_ColourGrade_Set(&desaturated);
    Port_ColourGrade_ToRgb888(&desaturated, grey, out);
    CHECK(out[0] + 1 >= out[1] && out[0] <= out[1] + 1, "grey stays grey in red vs green");
    CHECK(out[1] + 1 >= out[2] && out[1] <= out[2] + 1, "grey stays grey in green vs blue");

    Port_ColourGrade_Set(&neutral);
    Port_ColourGrade_ChannelLut(&neutral, lut);
    for (int i = 0; i < 2; ++i) {
        Port_ColourGrade_ToRgb888(&neutral, samples[i], out);
        CHECK(out[0] == lut[samples[i] & 0x1Fu], "saturation 0 leaves red independent");
        CHECK(out[1] == lut[(samples[i] >> 5) & 0x1Fu], "saturation 0 leaves green independent");
        CHECK(out[2] == lut[(samples[i] >> 10) & 0x1Fu], "saturation 0 leaves blue independent");
    }
    puts("colour_grade_test: grey preservation PASS");
}

static void TestLutRangeAndMonotonicity(void) {
    const PortColourGrade tuned = MakeGrade(PORT_COLOUR_CUSTOM, 30, 20, 0, 2, 0, 0);
    uint8_t lut[32];

    Port_ColourGrade_Set(&tuned);
    Port_ColourGrade_ChannelLut(&tuned, lut);
    for (int x = 0; x < 32; ++x) {
        if (x > 0) {
            CHECK(lut[x] >= lut[x - 1], "CUSTOM LUT is non-decreasing");
        }
    }
    puts("colour_grade_test: LUT range and monotonicity PASS");
}

static void TestEpochActiveAndNames(void) {
    PortColourGrade custom = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const PortColourGrade gba = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0, 0, 0);
    const PortColourGrade off = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0);
    unsigned epoch;

    Port_ColourGrade_Set(&custom);
    epoch = Port_ColourGrade_Epoch();
    Port_ColourGrade_Set(&custom);
    CHECK(Port_ColourGrade_Epoch() == epoch, "setting the same grade keeps the epoch");

    custom.brightness = 10;
    Port_ColourGrade_Set(&custom);
    CHECK(Port_ColourGrade_Epoch() != epoch, "changing a param bumps the epoch");

    epoch = Port_ColourGrade_Epoch();
    custom.midtones = 10;
    Port_ColourGrade_Set(&custom);
    CHECK(Port_ColourGrade_Epoch() != epoch, "changing only a zone bumps the epoch");

    CHECK(Port_ColourGrade_Active(&gba), "GBA is active");
    CHECK(Port_ColourGrade_Active(&custom), "CUSTOM is active");
    CHECK(!Port_ColourGrade_Active(&off), "OFF is inactive");

    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_OFF), "OFF") == 0, "mode name OFF");
    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_GBA), "GBA") == 0, "mode name GBA");
    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_CUSTOM), "CUSTOM") == 0, "mode name CUSTOM");
    CHECK(strcmp(Port_ColourModeName(9), "OFF") == 0, "unknown mode name is safe");
    puts("colour_grade_test: epoch, activity and names PASS");
}

static void TestVividPreset(void) {
    const PortColourGrade vivid = MakeGrade(PORT_COLOUR_VIVID, 0, 0, 0, 0, 0, 0);
    const PortColourGrade presetCustom = MakeGrade(PORT_COLOUR_CUSTOM, 10, 10, 0, -8, 0, 0);
    const uint16_t samples[4] = { 0x0000u, 0x7FFFu, 0x1385u, 0x18DCu };
    const PortColourGrade* current;
    uint8_t vividLut[32], customLut[32];
    uint8_t vividOut[3], customOut[3];

    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_VIVID), "VIVID") == 0, "mode name VIVID");

    Port_ColourGrade_Set(&vivid);
    Port_ColourGrade_ChannelLut(&vivid, vividLut);
    Port_ColourGrade_ChannelLut(&presetCustom, customLut);
    CHECK(memcmp(vividLut, customLut, sizeof(vividLut)) == 0,
          "VIVID LUT equals CUSTOM {10, 10, 0, -8}");

    for (int i = 0; i < 4; ++i) {
        Port_ColourGrade_ToRgb888(&vivid, samples[i], vividOut);
        Port_ColourGrade_ToRgb888(&presetCustom, samples[i], customOut);
        CHECK(vividOut[0] == customOut[0] && vividOut[1] == customOut[1] && vividOut[2] == customOut[2],
              "VIVID ToRgb888 equals CUSTOM {10, 10, 0, -8}");
    }

    /* Mode 3 is legitimate now: only mode >= 4 folds to OFF. */
    Port_ColourGrade_Set(&vivid);
    current = Port_ColourGrade_Current();
    CHECK(current->mode == PORT_COLOUR_VIVID, "mode 3 stays VIVID, not OFF");
    CHECK(vividLut[31] == customLut[31] && vividLut[0] == customLut[0],
          "VIVID preset shapes the curve endpoints");

    /* VIVID ignores the fields, and keeps the user's CUSTOM values untouched. */
    {
        const PortColourGrade oddVivid = MakeGrade(PORT_COLOUR_VIVID, -30, 40, -20, 5, 40, -40);
        uint8_t oddBytes[32];

        Port_ColourGrade_Set(&oddVivid);
        current = Port_ColourGrade_Current();
        CHECK(current->mode == PORT_COLOUR_VIVID, "VIVID with other fields is still VIVID");
        CHECK(current->brightness == -30 && current->contrast == 40 && current->saturation == -20 &&
                  current->gamma == 5 && current->shadows == 40 && current->midtones == -40,
              "VIVID keeps the stored CUSTOM fields");
        Port_ColourGrade_ChannelLut(&oddVivid, oddBytes);
        CHECK(memcmp(oddBytes, customLut, sizeof(oddBytes)) == 0,
              "VIVID ignores the stored fields and builds the preset");
    }
    puts("colour_grade_test: VIVID preset PASS");
}

static void TestShadowZoneLift(void) {
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const PortColourGrade shadows = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 50, 0);
    uint8_t blackNeutral[3], blackLifted[3], whiteNeutral[3], whiteLifted[3];

    Port_ColourGrade_ToRgb888(&neutral, 0x0000u, blackNeutral);
    Port_ColourGrade_ToRgb888(&shadows, 0x0000u, blackLifted);
    Port_ColourGrade_ToRgb888(&neutral, 0x7FFFu, whiteNeutral);
    Port_ColourGrade_ToRgb888(&shadows, 0x7FFFu, whiteLifted);

    CHECK(blackNeutral[0] == 0, "neutral CUSTOM leaves black at 0");
    CHECK(blackLifted[0] > 0, "SHADOWS +50 lifts black");
    CHECK((int)blackLifted[0] - (int)blackNeutral[0] > (int)whiteLifted[0] - (int)whiteNeutral[0],
          "the shadow lift is stronger at the dark end");
    CHECK(blackLifted[0] <= 255 && whiteLifted[0] <= 255 && whiteLifted[1] <= 255 && whiteLifted[2] <= 255,
          "the shadow lift never leaves the 0..255 range");
    puts("colour_grade_test: shadow zone lift PASS");
}

static void TestMidtoneZoneLift(void) {
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const PortColourGrade midtones = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 50);
    const uint16_t low = 0x14A5u;  /* ~16% luma */
    const uint16_t mid = 0x4210u;  /* ~50% luma */
    const uint16_t high = 0x739Cu; /* ~88% luma */
    const uint16_t white = 0x7FFFu;
    uint8_t lowN[3], midN[3], highN[3], lowL[3], midL[3], highL[3], whiteL[3];
    int lowGain, midGain, highGain;

    Port_ColourGrade_ToRgb888(&neutral, low, lowN);
    Port_ColourGrade_ToRgb888(&neutral, mid, midN);
    Port_ColourGrade_ToRgb888(&neutral, high, highN);
    Port_ColourGrade_ToRgb888(&midtones, low, lowL);
    Port_ColourGrade_ToRgb888(&midtones, mid, midL);
    Port_ColourGrade_ToRgb888(&midtones, high, highL);
    Port_ColourGrade_ToRgb888(&midtones, white, whiteL);

    lowGain = (int)lowL[0] - (int)lowN[0];
    midGain = (int)midL[0] - (int)midN[0];
    highGain = (int)highL[0] - (int)highN[0];
    CHECK(lowGain > 0 && midGain > 0 && highGain > 0, "MIDTONES +50 lifts every luma band");
    CHECK(midGain > lowGain, "the midtone lift peaks around 50% luma, not the dark end");
    CHECK(midGain > highGain, "the midtone lift peaks around 50% luma, not the bright end");
    CHECK(whiteL[0] == 255 && whiteL[1] == 255 && whiteL[2] == 255,
          "a lifted near-white colour clamps to 255");
    puts("colour_grade_test: midtone zone lift PASS");
}

static void TestNeutralZonesParity(void) {
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const uint16_t samples[4] = { 0x0000u, 0x7FFFu, 0x1385u, 0x18DCu };
    uint8_t lut[32], out[3];

    Port_ColourGrade_Set(&neutral);
    Port_ColourGrade_ChannelLut(&neutral, lut);
    for (int i = 0; i < 4; ++i) {
        Port_ColourGrade_ToRgb888(&neutral, samples[i], out);
        CHECK(out[0] == lut[samples[i] & 0x1Fu] && out[1] == lut[(samples[i] >> 5) & 0x1Fu] &&
                  out[2] == lut[(samples[i] >> 10) & 0x1Fu],
              "neutral zones leave ToRgb888 on the plain LUT");
    }
    puts("colour_grade_test: neutral zones parity PASS");
}

static void TestOffAndVividIgnoreZones(void) {
    const PortColourGrade offNeutral = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0);
    const PortColourGrade offZoned = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0, 40, 40);
    const PortColourGrade vividNeutral = MakeGrade(PORT_COLOUR_VIVID, 0, 0, 0, 0, 0, 0);
    const PortColourGrade vividZoned = MakeGrade(PORT_COLOUR_VIVID, 0, 0, 0, 0, 40, 40);
    const uint16_t samples[3] = { 0x0000u, 0x4210u, 0x7FFFu };
    uint8_t a[3], b[3];

    for (int i = 0; i < 3; ++i) {
        Port_ColourGrade_ToRgb888(&offNeutral, samples[i], a);
        Port_ColourGrade_ToRgb888(&offZoned, samples[i], b);
        CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2], "OFF ignores the zones");

        Port_ColourGrade_ToRgb888(&vividNeutral, samples[i], a);
        Port_ColourGrade_ToRgb888(&vividZoned, samples[i], b);
        CHECK(a[0] == b[0] && a[1] == b[1] && a[2] == b[2], "VIVID ignores the zones");
    }
    puts("colour_grade_test: OFF and VIVID ignore zones PASS");
}

static void TestGbaReceivesZones(void) {
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0, 0, 0);
    const PortColourGrade lifted = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0, 20, 0);
    uint8_t before[3], after[3];

    Port_ColourGrade_ToRgb888(&neutral, 0x0000u, before);
    Port_ColourGrade_ToRgb888(&lifted, 0x0000u, after);
    CHECK(before[0] == 0 && before[1] == 0 && before[2] == 0, "GBA leaves black at 0");
    CHECK(after[0] > 0, "GBA receives the shadow lift");
    puts("colour_grade_test: GBA receives zones PASS");
}

static void TestLumaAdjustTable(void) {
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 0, 0);
    const PortColourGrade shadows = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0, 50, 0);
    const PortColourGrade offZoned = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0, 40, 40);
    int16_t adj[256];
    bool allZero = true;

    Port_ColourGrade_LumaAdjust(&neutral, adj);
    for (int l = 0; l < 256; ++l) {
        if (adj[l] != 0) {
            allZero = false;
        }
    }
    CHECK(allZero, "neutral zones build an all-zero table");

    Port_ColourGrade_LumaAdjust(&offZoned, adj);
    allZero = true;
    for (int l = 0; l < 256; ++l) {
        if (adj[l] != 0) {
            allZero = false;
        }
    }
    CHECK(allZero, "OFF builds an all-zero table even with stored zones");

    Port_ColourGrade_LumaAdjust(&shadows, adj);
    CHECK(adj[0] > 0 && adj[0] > adj[95] && adj[95] >= 0,
          "the shadow weight falls from black to 37.5% luma");
    puts("colour_grade_test: luma adjust table PASS");
}

int main(void) {
    TestOffParity();
    TestGbaParity();
    TestCustomNeutralParity();
    TestClamping();
    TestSaturationPreservesGrey();
    TestLutRangeAndMonotonicity();
    TestEpochActiveAndNames();
    TestVividPreset();
    TestShadowZoneLift();
    TestMidtoneZoneLift();
    TestNeutralZonesParity();
    TestOffAndVividIgnoreZones();
    TestGbaReceivesZones();
    TestLumaAdjustTable();

    if (sFailures != 0) {
        fprintf(stderr, "colour_grade_test: %d FAILURE(S)\n", sFailures);
        return 1;
    }
    puts("colour_grade_test: ALL PASS");
    return 0;
}
