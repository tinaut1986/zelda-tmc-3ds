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
static int16_t s_luma_adj[256];
static unsigned s_epoch;
static bool s_lutValid;

/* VIVID's fixed curve. The grade's own fields are kept as the user set them so
 * returning to CUSTOM restores them; VIVID ignores them and uses this preset.
 * Gamma -8 is 1 + (-8)*0.05 = 0.6; the preset's zones are neutral. */
static const PortColourGrade kVividPreset = { PORT_COLOUR_VIVID, 10, 10, 0, -8, 0, 0 };

static PortColourGrade EffectiveGrade(const PortColourGrade* g) {
    return g->mode == PORT_COLOUR_VIVID ? kVividPreset : *g;
}

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
    out.gamma = (int8_t)ClampInt(g->gamma, -20, 20);
    out.shadows = (int8_t)ClampInt(g->shadows, -50, 50);
    out.midtones = (int8_t)ClampInt(g->midtones, -50, 50);
    return out;
}

static bool SameGrade(const PortColourGrade* a, const PortColourGrade* b) {
    return a->mode == b->mode && a->brightness == b->brightness && a->contrast == b->contrast &&
           a->saturation == b->saturation && a->gamma == b->gamma && a->shadows == b->shadows &&
           a->midtones == b->midtones;
}

/* One parameterised curve entry (CUSTOM and VIVID); the only place this module
 * uses floating point. */
static uint8_t CurveEntry(int x, const PortColourGrade* g) {
    double t = (double)(x << 3) / 255.0;
    t = pow(t, 1.0 / (1.0 + (double)g->gamma * 0.05));
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
    const PortColourGrade eff = EffectiveGrade(g);

    if (eff.mode == PORT_COLOUR_OFF) {
        for (int x = 0; x < 32; ++x) {
            lut[x] = (uint8_t)(x << 3);
        }
        return;
    }
    if (eff.mode == PORT_COLOUR_GBA) {
        memcpy(lut, kGbaLut, sizeof(kGbaLut));
        return;
    }
    for (int x = 0; x < 32; ++x) {
        lut[x] = CurveEntry(x, &eff);
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

/* Additive per-channel lift by post-curve luma l (0..255). Shadows fade linearly
 * from 1 at black to 0 at 128 (50%); midtones peak at 1 at 50% luma. Values are
 * rounded to the nearest integer, so a neutral zone table is all zero. */
static void BuildLumaAdjust(const PortColourGrade* g, int16_t adj[256]) {
    const double shadows = (double)g->shadows * 2.55; /* (shadows/100)*255 */
    const double midtones = (double)g->midtones * 2.55;

    if (g->mode == PORT_COLOUR_OFF) {
        memset(adj, 0, 256 * sizeof(adj[0]));
        return;
    }
    for (int l = 0; l < 256; ++l) {
        const double u = (double)l / 255.0;
        const double wM = 4.0 * u * (1.0 - u);
        double wS = 0.0;
        if (l < 128) {
            wS = (128.0 - (double)l) / 128.0;
        }
        const double value = shadows * wS + midtones * wM;
        adj[l] = (int16_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
    }
}

/* The installed table when the grade matches it, otherwise one built into scratch. */
static const int16_t* ResolveLumaAdjust(const PortColourGrade* g, int16_t scratch[256]) {
    const PortColourGrade clamped = ClampGrade(g);
    PortColourGrade eff;
    if (s_lutValid && SameGrade(&clamped, &s_current)) {
        return s_luma_adj;
    }
    eff = EffectiveGrade(&clamped);
    BuildLumaAdjust(&eff, scratch);
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
    {
        const PortColourGrade eff = EffectiveGrade(&s_current);
        BuildLumaAdjust(&eff, s_luma_adj);
    }
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
    PortColourGrade off = { PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0 };
    uint8_t scratch[32];

    if (g == NULL) {
        g = &off;
    }
    memcpy(lut, ResolveLut(g, scratch), sizeof(scratch));
}

void Port_ColourGrade_LumaAdjust(const PortColourGrade* g, int16_t adj[256]) {
    PortColourGrade off = { PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0 };
    PortColourGrade clamped;
    PortColourGrade eff;

    if (adj == NULL) {
        return;
    }
    if (g == NULL) {
        g = &off;
    }
    clamped = ClampGrade(g);
    eff = EffectiveGrade(&clamped);
    BuildLumaAdjust(&eff, adj);
}

void Port_ColourGrade_ToRgb888(const PortColourGrade* g, uint16_t rgb555, uint8_t out[3]) {
    PortColourGrade off = { PORT_COLOUR_OFF, 0, 0, 0, 0, 0, 0 };
    PortColourGrade clamped;
    PortColourGrade eff;
    uint8_t scratch[32];
    int16_t adjScratch[256];
    const uint8_t* lut;
    const int16_t* adj;
    unsigned r, gr, b;
    unsigned luma = 0;

    if (g == NULL) {
        g = &off;
    }
    clamped = ClampGrade(g);
    eff = EffectiveGrade(&clamped);
    lut = ResolveLut(&clamped, scratch);
    adj = ResolveLumaAdjust(&clamped, adjScratch);
    r = lut[rgb555 & 0x1Fu];
    gr = lut[(rgb555 >> 5) & 0x1Fu];
    b = lut[(rgb555 >> 10) & 0x1Fu];

    if (eff.mode != PORT_COLOUR_OFF) {
        luma = (299u * r + 587u * gr + 114u * b) / 1000u;
        r = ApplySaturation(r, luma, eff.saturation);
        gr = ApplySaturation(gr, luma, eff.saturation);
        b = ApplySaturation(b, luma, eff.saturation);
        if (adj[luma] != 0) {
            r = (unsigned)ClampInt((int)r + adj[luma], 0, 255);
            gr = (unsigned)ClampInt((int)gr + adj[luma], 0, 255);
            b = (unsigned)ClampInt((int)b + adj[luma], 0, 255);
        }
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
        case PORT_COLOUR_VIVID:
            return "VIVID";
        default:
            return "OFF";
    }
}
