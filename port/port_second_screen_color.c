#include "port_second_screen_color.h"

#include "port_runtime_config.h"

#include <stdbool.h>

/* The GBA LCD's curve, as in port/ppu/src/mode1.c (mode1_gba_lcd_lut). */
static const uint8_t kLcdLut[32] = {
    0, 0, 0, 0, 1, 2, 4, 8, 13, 19, 25, 32, 39, 46, 54, 63,
    71, 80, 90, 100, 110, 120, 131, 142, 154, 165, 178, 190, 203, 216, 229, 243,
};

uint32_t Port_SecondScreen_Rgb555(uint16_t c) {
    const unsigned r5 = c & 0x1Fu, g5 = (c >> 5) & 0x1Fu, b5 = (c >> 10) & 0x1Fu;
    const bool corrected = Port_Config_GetColorCorrection();
    const uint32_t r = corrected ? kLcdLut[r5] : (r5 << 3);
    const uint32_t g = corrected ? kLcdLut[g5] : (g5 << 3);
    const uint32_t b = corrected ? kLcdLut[b5] : (b5 << 3);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

unsigned Port_SecondScreen_ColorEpoch(void) {
    static int last = -1;
    static unsigned epoch;
    const int now = Port_Config_GetColorCorrection() ? 1 : 0;
    if (now != last) {
        last = now;
        ++epoch;
    }
    return epoch;
}
