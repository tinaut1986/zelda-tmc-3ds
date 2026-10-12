#include "boot_text_3ds.h"

#include <3ds.h>

#include <stddef.h>
#include <string.h>

#include "port_text_i18n.h"
#include "ui_font_3ds.h"

enum { SCREEN_W = 320, SCREEN_H = 240, LINE_H = 12, MAX_LINES = 18 };

uint16_t BootText_Rgb(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

PortTextLanguage BootText_Language(void) {
    u8 language = CFG_LANGUAGE_EN;
    if (R_FAILED(CFGU_GetSystemLanguage(&language))) return PORT_TEXT_EN;
    switch (language) {
        case CFG_LANGUAGE_FR: return PORT_TEXT_FR;
        case CFG_LANGUAGE_DE: return PORT_TEXT_DE;
        case CFG_LANGUAGE_ES: return PORT_TEXT_ES;
        case CFG_LANGUAGE_IT: return PORT_TEXT_IT;
        default: return PORT_TEXT_EN;
    }
}

const char* BootText_Tr(const char* english) {
    return Port_Text_TrFor(BootText_Language(), english);
}

/* The bottom framebuffer is stored rotated: column-major, bottom row first. */
static void PutPixel(uint16_t* fb, int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    fb[(SCREEN_H - 1 - y) + x * SCREEN_H] = color;
}

static int TextColumns(const char* text, size_t length) {
    int n = 0;
    for (const char* p = text; *p && (size_t)(p - text) < length;) {
        Port_Text_Utf8Next(&p);
        ++n;
    }
    return n;
}

static void DrawText(uint16_t* fb, int y, const char* text, uint16_t color) {
    int x = (SCREEN_W - TextColumns(text, strlen(text)) * kUiAdvance) / 2;
    for (const char* p = text; *p;) {
        const unsigned char c = Port_Text_Utf8Next(&p);
        const uint8_t* glyph = UiFont_Glyph(c);
        for (int gy = 0; glyph && gy < kUiGlyphH; ++gy)
            for (int gx = 0; gx < kUiGlyphW; ++gx)
                if (glyph[gy] & (0x10u >> gx)) PutPixel(fb, x + gx, y + gy, color);
        const uint8_t* mark = c >= 0xC0 ? UiFont_Mark(c) : NULL;
        for (int gy = 0; mark && gy < 2; ++gy)
            for (int gx = 0; gx < kUiGlyphW; ++gx)
                if (mark[gy] & (0x10u >> gx)) PutPixel(fb, x + gx, y - kUiMarkRise + gy, color);
        x += kUiAdvance;
    }
}

void BootText_Show(const BootTextLine* lines, int count) {
    const uint16_t background = BootText_Rgb(16, 16, 24);
    const uint16_t frame = BootText_Rgb(96, 80, 40);
    const int top = (SCREEN_H - count * LINE_H) / 2;
    /* Into both buffers of the swap chain (gfxInit's RGB565), so no swap shows the old one. */
    for (int pass = 0; pass < 2; ++pass) {
        u16 w = 0, h = 0;
        uint16_t* fb = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
        if (!fb) return;
        for (int i = 0; i < SCREEN_W * SCREEN_H; ++i) fb[i] = background;
        for (int x = 4; x < SCREEN_W - 4; ++x) {
            PutPixel(fb, x, 4, frame);
            PutPixel(fb, x, SCREEN_H - 5, frame);
        }
        for (int y = 4; y < SCREEN_H - 4; ++y) {
            PutPixel(fb, 4, y, frame);
            PutPixel(fb, SCREEN_W - 5, y, frame);
        }
        for (int i = 0; i < count; ++i)
            if (lines[i].text) DrawText(fb, top + i * LINE_H, lines[i].text, lines[i].color);
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
}

void BootText_ShowMessage(const char* title, uint16_t titleColor, const char* body, const char* footer) {
    /* Wrapped lines live in `storage`; each line is one NUL-terminated piece. */
    static char storage[MAX_LINES][BOOT_TEXT_MAX_COLUMNS * 2 + 1];
    BootTextLine lines[MAX_LINES];
    int count = 0;
    lines[count++] = (BootTextLine){ title, titleColor };
    lines[count++] = (BootTextLine){ NULL, 0 };
    const char* p = body ? body : "";
    const int bodyMax = MAX_LINES - (footer ? 2 : 0);
    while (*p && count < bodyMax) {
        /* One source line up to '\n', broken at the last space that fits. */
        const char* end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        size_t take = length;
        if (TextColumns(p, length) > BOOT_TEXT_MAX_COLUMNS) {
            size_t fit = 0;
            for (const char* q = p; (size_t)(q - p) < length && TextColumns(p, (size_t)(q - p)) < BOOT_TEXT_MAX_COLUMNS;)
                Port_Text_Utf8Next(&q), fit = (size_t)(q - p);
            take = fit;
            for (size_t i = fit; i > 0; --i)
                if (p[i] == ' ') {
                    take = i;
                    break;
                }
        }
        if (take >= sizeof(storage[0])) take = sizeof(storage[0]) - 1;
        memcpy(storage[count], p, take);
        storage[count][take] = '\0';
        lines[count] = (BootTextLine){ storage[count], BOOT_TEXT_TEXT };
        ++count;
        p += take;
        if (*p == ' ' || *p == '\n') ++p;
    }
    if (footer) {
        lines[count++] = (BootTextLine){ NULL, 0 };
        lines[count++] = (BootTextLine){ footer, BOOT_TEXT_FAINT };
    }
    BootText_Show(lines, count);
}
