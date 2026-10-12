#pragma once

#include <stdint.h>

#include "port_text_i18n.h"

/* Text on the bottom screen outside gameplay (the save import question, the fatal
 * messages): centred lines of the bottom UI's 5x7 font on a dark background, drawn
 * straight into the gfx framebuffer. Nothing else may draw there meanwhile (the boot
 * animation stopped). */

typedef struct {
    const char* text; /* UTF-8; NULL leaves the line empty */
    uint16_t color;   /* RGB565, BootText_Rgb */
} BootTextLine;

uint16_t BootText_Rgb(int r, int g, int b);

/* The console's language as the port's UI text knows it: before a save is loaded the
 * game's own language is not known yet. */
PortTextLanguage BootText_Language(void);

/* Port_Text_Tr in that language (an English key with no entry comes back as is). */
const char* BootText_Tr(const char* english);

enum { BOOT_TEXT_MAX_COLUMNS = 52 };
#define BOOT_TEXT_GOLD BootText_Rgb(248, 208, 64)
#define BOOT_TEXT_BAD BootText_Rgb(240, 96, 80)
#define BOOT_TEXT_TEXT BootText_Rgb(232, 232, 232)
#define BOOT_TEXT_FAINT BootText_Rgb(150, 150, 160)

void BootText_Show(const BootTextLine* lines, int count);

/* A title, a body wrapped at BOOT_TEXT_MAX_COLUMNS ('\n' breaks a line) and a footer. */
void BootText_ShowMessage(const char* title, uint16_t titleColor, const char* body, const char* footer);
