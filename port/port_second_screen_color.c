#include "port_second_screen_color.h"

#include "port_color_grade.h"

uint32_t Port_SecondScreen_Rgb555(uint16_t c) {
    uint8_t rgb[3];
    Port_ColourGrade_ToRgb888(Port_ColourGrade_Current(), c, rgb);
    return 0xFF000000u | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[0];
}

unsigned Port_SecondScreen_ColorEpoch(void) {
    return Port_ColourGrade_Epoch();
}
