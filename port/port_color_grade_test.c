/*
 * port/port_color_grade_test.c — locks the colour grade's byte-exact modes and clamping.
 *
 * OFF must stay identical to the raw 5-bit expansion, GBA to the fixed LCD LUT the
 * game shipped with, and a neutral CUSTOM grade to OFF. Out-of-range settings are
 * clamped by Port_ColourGrade_Set, saturation leaves greys alone, and the epoch only
 * moves when the installed grade differs.
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

static PortColourGrade MakeGrade(unsigned mode, int brightness, int contrast, int saturation, int gamma) {
    PortColourGrade g;
    g.mode = (uint8_t)mode;
    g.brightness = (int8_t)brightness;
    g.contrast = (int8_t)contrast;
    g.saturation = (int8_t)saturation;
    g.gamma = (int8_t)gamma;
    return g;
}

static void TestOffParity(void) {
    const PortColourGrade off = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0);
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
    const PortColourGrade gba = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0);
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
    const PortColourGrade custom = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0);
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
    const PortColourGrade wild = MakeGrade(9, 100, -99, 100, 99);
    const PortColourGrade* current;

    Port_ColourGrade_Set(&wild);
    current = Port_ColourGrade_Current();
    CHECK(current->mode == PORT_COLOUR_OFF, "invalid mode clamps to OFF");
    CHECK(current->brightness == 50, "brightness clamps high to 50");
    CHECK(current->contrast == -50, "contrast clamps low to -50");
    CHECK(current->saturation == 50, "saturation clamps high to 50");
    CHECK(current->gamma == 6, "gamma clamps high to 6");
    puts("colour_grade_test: clamping PASS");
}

static void TestSaturationPreservesGrey(void) {
    const PortColourGrade desaturated = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, -50, 0);
    const PortColourGrade neutral = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0);
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
    const PortColourGrade tuned = MakeGrade(PORT_COLOUR_CUSTOM, 30, 20, 0, 2);
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
    PortColourGrade custom = MakeGrade(PORT_COLOUR_CUSTOM, 0, 0, 0, 0);
    const PortColourGrade gba = MakeGrade(PORT_COLOUR_GBA, 0, 0, 0, 0);
    const PortColourGrade off = MakeGrade(PORT_COLOUR_OFF, 0, 0, 0, 0);
    unsigned epoch;

    Port_ColourGrade_Set(&custom);
    epoch = Port_ColourGrade_Epoch();
    Port_ColourGrade_Set(&custom);
    CHECK(Port_ColourGrade_Epoch() == epoch, "setting the same grade keeps the epoch");

    custom.brightness = 10;
    Port_ColourGrade_Set(&custom);
    CHECK(Port_ColourGrade_Epoch() != epoch, "changing a param bumps the epoch");

    CHECK(Port_ColourGrade_Active(&gba), "GBA is active");
    CHECK(Port_ColourGrade_Active(&custom), "CUSTOM is active");
    CHECK(!Port_ColourGrade_Active(&off), "OFF is inactive");

    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_OFF), "OFF") == 0, "mode name OFF");
    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_GBA), "GBA") == 0, "mode name GBA");
    CHECK(strcmp(Port_ColourModeName(PORT_COLOUR_CUSTOM), "CUSTOM") == 0, "mode name CUSTOM");
    CHECK(strcmp(Port_ColourModeName(9), "OFF") == 0, "unknown mode name is safe");
    puts("colour_grade_test: epoch, activity and names PASS");
}

int main(void) {
    TestOffParity();
    TestGbaParity();
    TestCustomNeutralParity();
    TestClamping();
    TestSaturationPreservesGrey();
    TestLutRangeAndMonotonicity();
    TestEpochActiveAndNames();

    if (sFailures != 0) {
        fprintf(stderr, "colour_grade_test: %d FAILURE(S)\n", sFailures);
        return 1;
    }
    puts("colour_grade_test: ALL PASS");
    return 0;
}
