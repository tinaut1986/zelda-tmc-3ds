/* Compact bottom-screen chrome for the 3DS, included by port/port_second_screen.c.
 *
 * The compositor was designed for a 720p Android panel and scaled by
 * u = min(w,h)/720, which on the 3DS's 320x240 left 16 px letters and room for
 * five rows. This file draws the parts that are UI rather than game art at the
 * screen's own size, in the style of the owner's other ports (../sm-3ds
 * bottom_ui.c, ../mzm port_bottom_ui_3ds.c): a slim tab bar on top, the 5x7 font
 * at 1x, and OPTIONS / DEBUG as grids of two-line cells (name over value).
 * The map, items and quest panels keep the game's art, inside the content
 * rectangle this file hands them.
 *
 * Everything here uses the compositor's own SSurf / TargetList / AddTarget and
 * its tap actions, so taps go through Port_SecondScreen_OnTap unchanged. */

#include "ui_font_3ds.h"
#include "tmc3ds_build_config.h"

/* Colours, RGBA_8888 as the compositor stores them (RGB() above). */
#define CC_BG        RGB(16, 20, 26)
#define CC_BAR       RGB(10, 13, 18)
#define CC_TAB       RGB(32, 40, 52)
#define CC_TAB_EDGE  RGB(60, 74, 92)
#define CC_TAB_ON    RGB(40, 112, 68)
#define CC_TAB_ON_EDGE RGB(110, 200, 130)
#define CC_CELL      RGB(26, 32, 42)
#define CC_CELL_EDGE RGB(64, 80, 100)
#define CC_BTN       RGB(22, 56, 42)
#define CC_BTN_EDGE  RGB(70, 150, 100)
#define CC_TEXT      RGB(232, 236, 240)
#define CC_DIM       RGB(140, 150, 166)
#define CC_FAINT     RGB(84, 94, 110)
#define CC_GOOD      RGB(120, 220, 130)
#define CC_WARN      RGB(240, 200, 80)
#define CC_BAD       RGB(240, 110, 100)
#define CC_ACCENT    RGB(110, 200, 240)

enum {
    CC_TAB_H = 18,     /* tab bar height; the content starts 2 px below */
    CC_TOP = CC_TAB_H + 2,
    CC_MARGIN = 3,
    CC_GAP = 3,
    CC_CELL_H = 25,    /* name row + value row */
};

/* ---- text: the 5x7 font at 1x, 6 px per character ---- */

static int32_t CText(const SSurf* s, int32_t x, int32_t y, uint32_t color, const char* str) {
    int32_t cx = x;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        const uint8_t* g = UiFont_Glyph(*p);
        if (g) {
            for (int32_t gy = 0; gy < kUiGlyphH; gy++) {
                for (int32_t gx = 0; gx < kUiGlyphW; gx++) {
                    if (g[gy] & (0x10u >> gx)) {
                        FillRect(s, cx + gx, y + gy, cx + gx + 1, y + gy + 1, color);
                    }
                }
            }
        }
        cx += kUiAdvance;
    }
    return cx - x;
}

static int32_t CTextWidth(const char* str) {
    int32_t n = (int32_t)strlen(str);
    return n > 0 ? n * kUiAdvance - 1 : 0;
}

static void CTextCentered(const SSurf* s, int32_t cx, int32_t y, uint32_t color, const char* str) {
    CText(s, cx - CTextWidth(str) / 2, y, color, str);
}

/* ---- boxes ---- */

/* 1 px border, flat body, a 1 px lighter line under the top edge (mzm style). */
static void CBox(const SSurf* s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t body, uint32_t edge) {
    FillRect(s, x, y, x + w, y + h, edge);
    FillRect(s, x + 1, y + 1, x + w - 1, y + h - 1, body);
    FillRect(s, x + 1, y + 1, x + w - 1, y + 2, MixColor(body, edge, 96));
}

/* A two-line cell: the name dimmed on top, the value under it in `valueColor`.
 * action 0 = information only (no tap target, darker edge). */
static void CCell(const SSurf* s, TargetList* tl, int32_t x, int32_t y, int32_t w, int32_t h, const char* name,
                  const char* value, uint32_t valueColor, uint8_t action, uint8_t arg) {
    CBox(s, x, y, w, h, CC_CELL, action ? CC_CELL_EDGE : CC_TAB);
    CText(s, x + 4, y + 4, CC_DIM, name);
    CText(s, x + 4, y + 14, valueColor, value);
    if (action) {
        AddTarget(tl, (float)x, (float)y, (float)(x + w), (float)(y + h), action, arg);
    }
}

/* A one-line button with a centred label. */
static void CButton(const SSurf* s, TargetList* tl, int32_t x, int32_t y, int32_t w, int32_t h, const char* label,
                    uint32_t textColor, uint8_t action, uint8_t arg) {
    CBox(s, x, y, w, h, CC_BTN, CC_BTN_EDGE);
    CTextCentered(s, x + w / 2, y + (h - kUiGlyphH) / 2, textColor, label);
    AddTarget(tl, (float)x, (float)y, (float)(x + w), (float)(y + h), action, arg);
}

/* Column `col` of `cols` across the content width: x and w, the gaps shared out. */
static void CColumn(int32_t width, int cols, int col, int span, int32_t* x, int32_t* w) {
    int32_t inner = width - 2 * CC_MARGIN - (cols - 1) * CC_GAP;
    int32_t x0 = CC_MARGIN + (inner * col) / cols + col * CC_GAP;
    int32_t x1 = CC_MARGIN + (inner * (col + span)) / cols + (col + span - 1) * CC_GAP;
    *x = x0;
    *w = x1 - x0;
}

/* ---- tab bar ---- */

static int CompactTabCount(void) {
    return TMC3DS_DEBUG_TOOLS ? 5 : 4;
}

/* Set by the 3DS glue (port_second_screen_3ds.c) while OPTIONS is open from
 * the title screen: there are no game tabs then, only a way back. The BACK
 * box is drawn here, but its tap is the glue's (IsIdleBackTap), so it adds
 * no target. */
static uint8_t sCompactIdle;

static void Compact_IdleBackRect(int32_t* x0, int32_t* y0, int32_t* x1, int32_t* y1) {
    *x0 = CC_MARGIN;
    *y0 = 1;
    *x1 = CC_MARGIN + 64;
    *y1 = CC_TAB_H;
}

static void Compact_PaintTabBar(const SSurf* s, TargetList* tl, int activeTab, int settingsPage) {
    static const char* const kNames[5] = { "MAP", "ITEMS", "QUEST", "OPTIONS", "DEBUG" };
    static const uint8_t kTabs[5] = { SS_TAB_MAP, SS_TAB_ITEMS, SS_TAB_QUEST, SS_TAB_SETTINGS, SS_TAB_DEBUG };
    int n = CompactTabCount();
    FillRect(s, 0, 0, s->w, CC_TAB_H + 1, CC_BAR);
    if (sCompactIdle) {
        if (settingsPage == SS_SETTINGS_ROOT) {
            int32_t x0, y0, x1, y1;
            Compact_IdleBackRect(&x0, &y0, &x1, &y1);
            CBox(s, x0, y0, x1 - x0, y1 - y0, CC_BTN, CC_BTN_EDGE);
            CTextCentered(s, (x0 + x1) / 2, y0 + (y1 - y0 - kUiGlyphH) / 2, CC_TEXT, "< BACK");
        }
        CTextCentered(s, s->w / 2, 1 + (CC_TAB_H - 1 - kUiGlyphH) / 2, CC_TEXT, "OPTIONS");
        return;
    }
    for (int i = 0; i < n; i++) {
        int32_t x, w;
        CColumn(s->w, n, i, 1, &x, &w);
        int on = activeTab == kTabs[i];
        CBox(s, x, 1, w, CC_TAB_H - 1, on ? CC_TAB_ON : CC_TAB, on ? CC_TAB_ON_EDGE : CC_TAB_EDGE);
        CTextCentered(s, x + w / 2, 1 + (CC_TAB_H - 1 - kUiGlyphH) / 2, on ? CC_TEXT : CC_DIM, kNames[i]);
        AddTarget(tl, (float)x, 0.0f, (float)(x + w), (float)(CC_TAB_H + 1), SS_ACT_TAB, kTabs[i]);
    }
}

/* ---- OPTIONS ---- */

/* The value of a setting cell and its colour: green for "on", the plain text
 * colour for a choice, dimmed for "off". Shorter words than the old rows, so a
 * value fits a quarter-width cell. */
static uint32_t CompactSettingValue(int setting, char* out, int cap) {
    int on = GetSettingState(setting, out, cap);
    switch (setting) {
        case SS_SET_ASPECT_RATIO:
            return CC_TEXT;
        case SS_SET_DISPLAY_STYLE:
            if (Port_Config_Get3DSDisplayStyle() == PORT_3DS_DISPLAY_PIXEL_PERFECT) snprintf(out, (size_t)cap, "PIXEL P.");
            return CC_TEXT;
        case SS_SET_BACKDROP:
            return CC_TEXT;
        case SS_SET_TURBO:
            return CC_WARN;
        case SS_SET_VOLUME:
            snprintf(out + strlen(out), (size_t)cap - strlen(out), "%%");
            return on ? CC_GOOD : CC_DIM;
        case SS_SET_TOP_HUD:
            return on ? CC_GOOD : CC_WARN;
        default:
            return on ? CC_GOOD : CC_DIM;
    }
}

static int32_t CRowY(int row) {
    return CC_TOP + 2 + row * (CC_CELL_H + CC_GAP);
}

static void CSettingCellAt(const SSurf* s, TargetList* tl, int32_t y, int col, int span, const char* name,
                           int setting) {
    int32_t x, w;
    char value[24];
    CColumn(s->w, 4, col, span, &x, &w);
    uint32_t color = CompactSettingValue(setting, value, sizeof(value));
    CCell(s, tl, x, y, w, CC_CELL_H, name, value, color, SS_ACT_SETTING, (uint8_t)setting);
}

static void CSettingCell(const SSurf* s, TargetList* tl, int row, int col, int span, const char* name, int setting) {
    CSettingCellAt(s, tl, CRowY(row), col, span, name, setting);
}

static void Compact_PaintOptions(const SSurf* s, TargetList* tl) {
    FillRect(s, 0, CC_TOP, s->w, s->h, CC_BG);
    /* Screen */
    CSettingCell(s, tl, 0, 0, 1, "VIEW", SS_SET_ASPECT_RATIO);
    CSettingCell(s, tl, 0, 1, 1, "STYLE", SS_SET_DISPLAY_STYLE);
    CSettingCell(s, tl, 0, 2, 1, "TOP HUD", SS_SET_TOP_HUD);
    CSettingCell(s, tl, 0, 3, 1, "COLOURS", SS_SET_COLOR_CORRECTION);
    /* Game */
    CSettingCell(s, tl, 1, 0, 1, "TURBO", SS_SET_TURBO);
    CSettingCell(s, tl, 1, 1, 1, "VOLUME", SS_SET_VOLUME);
    CSettingCell(s, tl, 1, 2, 1, "AUTOSAVE", SS_SET_AUTOSAVE);
    CSettingCell(s, tl, 1, 3, 1, "HOLD TEXT", SS_SET_HOLD_ADVANCE);
    /* Map panel */
    CSettingCell(s, tl, 2, 0, 1, "MAP FOLLOW", SS_SET_FOLLOW);
    CSettingCell(s, tl, 2, 1, 1, "CREST PINS", SS_SET_CRESTS);
    CSettingCell(s, tl, 2, 2, 1, "FLOOR BACK", SS_SET_FLOOR_RETURN);
    CSettingCell(s, tl, 2, 3, 1, "SHOW FPS", SS_SET_SHOW_FPS);
    /* Look and system */
    CSettingCell(s, tl, 3, 0, 2, "PANEL BACKDROP", SS_SET_BACKDROP);
    {
        int32_t x, w;
        CColumn(s->w, 4, 2, 2, &x, &w);
        UpdateStatus st;
        Updater_GetStatus(&st);
        const char* value = st.state == UPDATE_AVAILABLE ? "NEW VERSION" : (st.prerelease ? "+ BETAS" : "STABLE");
        CCell(s, tl, x, CRowY(3), w, CC_CELL_H, "UPDATES",
              value, st.state == UPDATE_AVAILABLE ? CC_WARN : CC_TEXT, SS_ACT_SETTINGS_PAGE, SS_SETTINGS_UPDATE);
    }

    /* Footer: who we are and what we are built on. */
    char line[64];
    int32_t y = s->h - 3 * 10 - 2;
    CTextCentered(s, s->w / 2, y, CC_DIM, "MINISH CAP 3DS");
    snprintf(line, sizeof(line), "V%s", TMC_PORT_VERSION_LABEL);
    CTextCentered(s, s->w / 2, y + 10, CC_TEXT, line);
    snprintf(line, sizeof(line), "BASED ON ESTEBANPDN %s%s", TMC_UPSTREAM_VERSION,
             TMC3DS_DEBUG_TOOLS ? "  - DEBUG BUILD" : "");
    CTextCentered(s, s->w / 2, y + 20, TMC3DS_DEBUG_TOOLS ? CC_WARN : CC_FAINT, line);
}

/* ---- DEBUG (debug-tools builds) ---- */

#if TMC3DS_DEBUG_TOOLS
static void Compact_PaintDebug(const SSurf* s, TargetList* tl, const SecondScreenSnapshot* snap, uint32_t tick,
                               uint32_t dumpFlashUntil, uint32_t loadStateFlashUntil, int loadStateResult) {
    FillRect(s, 0, CC_TOP, s->w, s->h, CC_BG);
    char v[7][24];
    double now = Port_PPU_3DS_CurrentFps(), avg = Port_PPU_3DS_AverageFps();
    snprintf(v[0], sizeof(v[0]), "%s", Platform3DS_IsNew3DS() ? "NEW 3DS" : "OLD 3DS");
    snprintf(v[1], sizeof(v[1]), "%.1f", now);
    snprintf(v[2], sizeof(v[2]), "%.1f", avg);
    snprintf(v[3], sizeof(v[3]), "%u%%", Platform3DS_Core1TimeLimit());
    snprintf(v[4], sizeof(v[4]), "%s", Port_Config_WidescreenEnabled() ? "WIDE" : "NATIVE");
    snprintf(v[5], sizeof(v[5]), "%02X / %02X", snap->area, snap->room);
    snprintf(v[6], sizeof(v[6]), "%d,%d", (int)snap->playerX, (int)snap->playerY);
    static const char* const kNames[7] = { "CONSOLE", "FPS NOW", "FPS AVG", "CORE 1", "VIEW", "AREA/ROOM", "LINK X,Y" };
    for (int i = 0; i < 7; i++) {
        int32_t x, w;
        CColumn(s->w, 4, i % 4, 1, &x, &w);
        uint32_t color = CC_TEXT;
        if (i == 1 || i == 2) {
            double f = i == 1 ? now : avg;
            color = f >= 59.0 ? CC_GOOD : (f >= 50.0 ? CC_WARN : CC_BAD);
        }
        CCell(s, tl, x, CRowY(i / 4), w, CC_CELL_H, kNames[i], v[i], color, 0, 0);
    }

    int32_t y = CRowY(2) + 4;
    CText(s, CC_MARGIN + 1, y, CC_FAINT, "TOOLS");
    y += 10;
    {
        int32_t x, w;
        const char* dump = (int32_t)(dumpFlashUntil - tick) > 0 ? "WRITTEN" : "WRITE";
        CColumn(s->w, 4, 0, 1, &x, &w);
        CCell(s, tl, x, y, w, CC_CELL_H, "MEM DUMP", dump, (int32_t)(dumpFlashUntil - tick) > 0 ? CC_GOOD : CC_ACCENT,
              SS_ACT_DEVELOPER_DUMP, 0);
        const char* load = (int32_t)(loadStateFlashUntil - tick) > 0
                               ? Port_DumpState_ResultLabel((PortDumpStateResult)loadStateResult)
                               : "LAST DUMP";
        CColumn(s->w, 4, 1, 1, &x, &w);
        CCell(s, tl, x, y, w, CC_CELL_H, "LOAD STATE", load, CC_ACCENT, SS_ACT_DEVELOPER_LOAD, 0);
    }
    CSettingCellAt(s, tl, y, 2, 1, "SHOW FPS", SS_SET_SHOW_FPS);
}
#endif
