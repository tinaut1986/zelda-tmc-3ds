#include "port_color_grade.h"

#include <math.h>
#include <string.h>

/* The GBA LCD's fixed 5-bit response curve (the same table the game was tuned for). */
static const uint8_t kGbaLut[32] = {
    0, 0, 0, 0, 1, 2, 4, 8, 13, 19, 25, 32, 39, 46, 54, 63,
    71, 80, 90, 100, 110, 120, 131, 142, 154, 165, 178, 190, 203, 216, 229, 243,
};

/* Zero-initialised: OFF with neutral parameters, epoch 0, no LUT built yet. */
static PortColourGrade s_current;
static uint8_t s_lut[32];
static unsigned s_epoch;
static bool s_lutValid;

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static PortColourGrade ClampGrade(const PortColourGrade* g) {
    PortColourGrade out;
    out.mode = g->mode < PORT_COLOUR_MODE_COUNT ? g->mode : (uint8_t)PORT_COLOUR_OFF;
    out.brightness = (int8_t)ClampInt(g->brightness, -50, 50);
    out.contrast = (int8_t)ClampInt(g->contrast, -50, 50);
    out.saturation = (int8_t)ClampInt(g->saturation, -50, 50);
    out.gamma = (int8_t)ClampInt(g->gamma, -6, 6);
    return out;
}

static bool SameGrade(const PortColourGrade* a, const PortColourGrade* b) {
    return a->mode == b->mode && a->brightness == b->brightness && a->contrast == b->contrast &&
           a->saturation == b->saturation && a->gamma == b->gamma;
}

/* One CUSTOM curve entry; the only place this module uses floating point. */
static uint8_t CurveEntry(int x, const PortColourGrade* g) {
    double t = (double)(x << 3) / 255.0;
    t = pow(t, 1.0 / (1.0 + (double)g->gamma * 0.1));
    t = (t - 0.5) * (1.0 + (double)g->contrast / 50.0) + 0.5;
    t += (double)g->brightness / 100.0;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    return (uint8_t)(t * 255.0 + 0.5);
}

static void BuildLut(const PortColourGrade* g, uint8_t lut[32]) {
    if (g->mode == PORT_COLOUR_OFF) {
        for (int x = 0; x < 32; ++x) {
            lut[x] = (uint8_t)(x << 3);
        }
        return;
    }
    if (g->mode == PORT_COLOUR_GBA) {
        memcpy(lut, kGbaLut, sizeof(kGbaLut));
        return;
    }
    for (int x = 0; x < 32; ++x) {
        lut[x] = CurveEntry(x, g);
    }
}

/* The installed LUT when the grade matches it, otherwise one built into scratch. */
static const uint8_t* ResolveLut(const PortColourGrade* g, uint8_t scratch[32]) {
    const PortColourGrade clamped = ClampGrade(g);
    if (s_lutValid && SameGrade(&clamped, &s_current)) {
        return s_lut;
    }
    BuildLut(&clamped, scratch);
    return scratch;
}

/* Luma-preserving saturation mix in 8.8 fixed point. */
static uint8_t ApplySaturation(unsigned c, unsigned luma, int saturation) {
    const int factor = ((50 + saturation) * 256) / 50;
    const int scaled = (((int)c - (int)luma) * factor) >> 8;
    return (uint8_t)ClampInt((int)luma + scaled, 0, 255);
}

void Port_ColourGrade_Set(const PortColourGrade* g) {
    PortColourGrade clamped;

    if (g == NULL) {
        return;
    }
    clamped = ClampGrade(g);
    if (s_lutValid && SameGrade(&clamped, &s_current)) {
        return;
    }
    s_current = clamped;
    BuildLut(&s_current, s_lut);
    s_lutValid = true;
    ++s_epoch;
}

const PortColourGrade* Port_ColourGrade_Current(void) {
    return &s_current;
}

unsigned Port_ColourGrade_Epoch(void) {
    return s_epoch;
}

bool Port_ColourGrade_Active(const PortColourGrade* g) {
    return g != NULL && g->mode != PORT_COLOUR_OFF;
}

void Port_ColourGrade_ChannelLut(const PortColourGrade* g, uint8_t lut[32]) {
    PortColourGrade off = { PORT_COLOUR_OFF, 0, 0, 0, 0 };
    uint8_t scratch[32];

    if (g == NULL) {
        g = &off;
    }
    memcpy(lut, ResolveLut(g, scratch), sizeof(scratch));
}

void Port_ColourGrade_ToRgb888(const PortColourGrade* g, uint16_t rgb555, uint8_t out[3]) {
    PortColourGrade off = { PORT_COLOUR_OFF, 0, 0, 0, 0 };
    PortColourGrade clamped;
    uint8_t scratch[32];
    const uint8_t* lut;
    unsigned r, gr, b;

    if (g == NULL) {
        g = &off;
    }
    clamped = ClampGrade(g);
    lut = ResolveLut(&clamped, scratch);
    r = lut[rgb555 & 0x1Fu];
    gr = lut[(rgb555 >> 5) & 0x1Fu];
    b = lut[(rgb555 >> 10) & 0x1Fu];

    if (clamped.mode != PORT_COLOUR_OFF) {
        const unsigned luma = (299u * r + 587u * gr + 114u * b) / 1000u;
        r = ApplySaturation(r, luma, clamped.saturation);
        gr = ApplySaturation(gr, luma, clamped.saturation);
        b = ApplySaturation(b, luma, clamped.saturation);
    }

    out[0] = (uint8_t)r;
    out[1] = (uint8_t)gr;
    out[2] = (uint8_t)b;
}

const char* Port_ColourModeName(unsigned mode) {
    switch (mode) {
        case PORT_COLOUR_OFF:
            return "OFF";
        case PORT_COLOUR_GBA:
            return "GBA";
        case PORT_COLOUR_CUSTOM:
            return "CUSTOM";
        default:
            return "OFF";
    }
}
