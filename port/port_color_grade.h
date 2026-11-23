#pragma once
/* The colour grade applied to the game's 15-bit palettes: OFF keeps the raw 5-bit
 * expansion, GBA reproduces the fixed LCD curve the game was tuned for, CUSTOM
 * shapes that curve with brightness, contrast, saturation, gamma, shadows and
 * midtones, and VIVID is a fixed CUSTOM preset (brightness +10, contrast +10,
 * saturation 0, gamma 0.6, neutral zones). GBA, CUSTOM and VIVID share the
 * channel LUT; saturation and the shadow/midtone lifts are luma-driven passes on
 * top of it.
 *
 * Float math happens only when the 32-entry LUT and the 256-entry luma table are
 * rebuilt (Port_ColourGrade_Set); ChannelLut/ToRgb888/LumaAdjust are integer
 * lookups. Platform-neutral: <stdint.h> and <stdbool.h> only. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_COLOUR_OFF = 0,
    PORT_COLOUR_GBA,
    PORT_COLOUR_CUSTOM,
    PORT_COLOUR_VIVID, /* = 3; appended so persisted mode values never shift */
    PORT_COLOUR_MODE_COUNT,
} PortColourMode;

typedef struct {
    uint8_t mode;
    int8_t brightness; /* -50..+50, percent of the full range */
    int8_t contrast;   /* -50..+50, percent scale around mid-grey */
    int8_t saturation; /* -50..+50, 0 leaves colours untouched */
    int8_t gamma;      /* -20..+20, one twentieth per step: factor = 1 + gamma*0.05.
                        * Shown in hundredths as 100 + gamma*5 (0.6 = -8, 1.25 = +5). */
    int8_t shadows;    /* -50..+50, additive lift weighted to the dark end, 0 neutral */
    int8_t midtones;   /* -50..+50, additive lift peaked at 50% luma, 0 neutral */
} PortColourGrade;

/* Clamps every field, installs the grade and bumps the epoch only if it changed.
 * An unknown mode becomes PORT_COLOUR_OFF. */
void Port_ColourGrade_Set(const PortColourGrade* g);
/* The installed (already clamped) grade. */
const PortColourGrade* Port_ColourGrade_Current(void);
/* Changes whenever the installed grade changes; caches keyed on it are remade. */
unsigned Port_ColourGrade_Epoch(void);
/* True when the grade alters colours (mode is not OFF). */
bool Port_ColourGrade_Active(const PortColourGrade* g);
/* The 32-entry per-channel curve for this grade. The zones do not affect an
 * isolated channel, so shadows/midtones never enter this LUT. */
void Port_ColourGrade_ChannelLut(const PortColourGrade* g, uint8_t lut[32]);
/* Additive per-channel lift by post-curve luma, 0 when the zones are neutral.
 * The table is indexed by luma 0..255; callers cache it by Port_ColourGrade_Epoch(). */
void Port_ColourGrade_LumaAdjust(const PortColourGrade* g, int16_t adj[256]);
/* One RGB555 game colour as three 8-bit channels after the grade. */
void Port_ColourGrade_ToRgb888(const PortColourGrade* g, uint16_t rgb555, uint8_t out[3]);
/* "OFF", "GBA", "CUSTOM" or "VIVID"; an unknown mode reports "OFF". */
const char* Port_ColourModeName(unsigned mode);

#ifdef __cplusplus
}
#endif
