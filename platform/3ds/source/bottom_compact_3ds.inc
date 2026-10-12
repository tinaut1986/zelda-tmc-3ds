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

#include <stdarg.h>
#include <time.h>

/* Colours, RGBA_8888 as the compositor stores them (RGB() above). A theme is
 * a palette plus where the tab bar goes; the owner picks one (2026-10-11:
 * proposals rendered with tools/ui-preview, THEME=n). */
typedef struct CcTheme {
    const char* name;
    uint32_t bg, bar, tab, tabEdge, tabOn, tabOnEdge, tabOnText, cell, cellEdge, btn, btnEdge;
    uint32_t text, dim, faint, good, warn, bad, accent;
    uint8_t tabsBottom;
    uint8_t backdrop; /* SS_BACKDROP_* the map/items/quest panels sit on */
} CcTheme;

static const CcTheme kCcThemes[] = {
    { "SLATE", RGB(16, 20, 26), RGB(10, 13, 18), RGB(32, 40, 52), RGB(60, 74, 92), RGB(40, 112, 68),
      RGB(110, 200, 130), RGB(232, 236, 240), RGB(26, 32, 42), RGB(64, 80, 100), RGB(22, 56, 42),
      RGB(70, 150, 100), RGB(232, 236, 240), RGB(140, 150, 166), RGB(84, 94, 110), RGB(120, 220, 130),
      RGB(240, 200, 80), RGB(240, 110, 100), RGB(110, 200, 240), 0, SS_BACKDROP_SLATE },
    { "PARCHMENT", RGB(226, 212, 172), RGB(88, 60, 36), RGB(196, 176, 128), RGB(120, 92, 60), RGB(54, 112, 56),
      RGB(24, 64, 28), RGB(244, 240, 220), RGB(242, 232, 200), RGB(150, 118, 76), RGB(70, 128, 70),
      RGB(36, 78, 38), RGB(50, 38, 28), RGB(122, 96, 68), RGB(168, 146, 112), RGB(30, 120, 44),
      RGB(160, 100, 14), RGB(176, 46, 36), RGB(36, 86, 156), 0, SS_BACKDROP_PARCHMENT },
    { "NIGHT GOLD", RGB(12, 16, 34), RGB(6, 8, 20), RGB(26, 34, 70), RGB(52, 66, 120), RGB(142, 104, 26),
      RGB(240, 200, 90), RGB(255, 246, 220), RGB(20, 26, 54), RGB(56, 70, 124), RGB(40, 50, 100),
      RGB(110, 130, 210), RGB(236, 236, 246), RGB(140, 148, 180), RGB(76, 84, 120), RGB(120, 220, 150),
      RGB(250, 210, 90), RGB(240, 110, 100), RGB(110, 190, 255), 0, SS_BACKDROP_NAVY },
    { "FOREST", RGB(14, 26, 18), RGB(8, 16, 10), RGB(28, 48, 32), RGB(56, 90, 60), RGB(60, 140, 70),
      RGB(150, 220, 140), RGB(240, 246, 230), RGB(22, 38, 26), RGB(60, 100, 66), RGB(40, 90, 50),
      RGB(110, 180, 110), RGB(236, 240, 224), RGB(150, 170, 140), RGB(80, 100, 80), RGB(150, 230, 120),
      RGB(240, 200, 80), RGB(240, 110, 100), RGB(120, 210, 230), 1, SS_BACKDROP_DARK },
};
enum { CC_THEME_COUNT = (int)(sizeof(kCcThemes) / sizeof(kCcThemes[0])) };
static const CcTheme* sCc = &kCcThemes[0];

void Port_SecondScreen_3DS_SetUiTheme(int theme) {
    sCc = &kCcThemes[theme >= 0 && theme < CC_THEME_COUNT ? theme : 0];
}

/* For tools/ui-preview: the theme's name and the row a tab tap lands on. */
const char* Port_SecondScreen_3DS_UiThemeName(void) {
    return sCc->name;
}
int Port_SecondScreen_3DS_UiTabCenterY(int height);

#define CC_BG          (sCc->bg)
#define CC_BAR         (sCc->bar)
#define CC_TAB         (sCc->tab)
#define CC_TAB_EDGE    (sCc->tabEdge)
#define CC_TAB_ON      (sCc->tabOn)
#define CC_TAB_ON_EDGE (sCc->tabOnEdge)
#define CC_TAB_ON_TEXT (sCc->tabOnText)
#define CC_CELL        (sCc->cell)
#define CC_CELL_EDGE   (sCc->cellEdge)
#define CC_BTN         (sCc->btn)
#define CC_BTN_EDGE    (sCc->btnEdge)
#define CC_TEXT        (sCc->text)
#define CC_DIM         (sCc->dim)
#define CC_FAINT       (sCc->faint)
#define CC_GOOD        (sCc->good)
#define CC_WARN        (sCc->warn)
#define CC_BAD         (sCc->bad)
#define CC_ACCENT      (sCc->accent)

enum {
    CC_TAB_H = 24,     /* tab bar height; the content starts 2 px away from it */
    CC_MARGIN = 3,
    CC_GAP = 3,
    CC_CELL_H = 25,    /* name row + value row */
};

/* Where the tab bar and the content go for the current theme. */
static int32_t CcTabY(int32_t h) {
    return sCc->tabsBottom ? h - CC_TAB_H : 0;
}
static int32_t CcContentTop(void) {
    return sCc->tabsBottom ? 0 : CC_TAB_H;
}
static int32_t CcContentBottom(int32_t h) {
    return sCc->tabsBottom ? h - CC_TAB_H : h;
}
int Port_SecondScreen_3DS_UiTabCenterY(int height) {
    return CcTabY(height) + CC_TAB_H / 2;
}

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
/* mzm's and sm-3ds's layout: 30x20 icon tabs from the left, then the theme
 * button, and the clock, Wi-Fi bars and battery on the right. */

static int CompactTabCount(void) {
    return TMC3DS_DEBUG_TOOLS ? 6 : 5;
}

/* Set by the 3DS glue (port_second_screen_3ds.c) while no game runs (intro,
 * title, file select): every tab still shows; MAP, ITEMS and QUEST paint
 * Compact_PaintNoGame instead of reading the (zeroed) snapshot. */
static uint8_t sCompactNoGame;

enum { CC_TAB_W = 30, CC_TAB_STEP = 34, CC_TAB_X0 = 4 };

/* 13x12 pixel-art icons, '#' = ink. */
static const char* const kIconItems[12] = {
    "......#......", ".....###.....", ".....###.....", ".....###.....", ".....###.....", ".....###.....",
    ".....###.....", "..#########..", "..#########..", ".....###.....", ".....###.....", "....#####....",
};
static const char* const kIconQuest[12] = {
    ".............", "..###...###..", ".#####.#####.", "#############", "#############", "#############",
    ".###########.", "..#########..", "...#######...", "....#####....", ".....###.....", "......#......",
};
static const char* const kIconMap[12] = {
    "####.###.####", "####.###.####", "####.###.####", ".............", "####.....####", "####.....####",
    "####.....####", ".............", "####.###.####", "####.###.####", "####.###.####", ".............",
};
static const char* const kIconOptions[12] = {
    ".............", "...###.......", "#############", "...###.......", ".............", "........###..",
    "#############", "........###..", ".............", ".###.........", "#############", ".###.........",
};
static const char* const kIconDebug[12] = {
    "....#...#....", ".....###.....", "....#####....", "#..#######..#", ".#.###.###.#.", "...###.###...",
    "####.....####", "...###.###...", ".#.###.###.#.", "#..#######..#", "....#####....", ".............",
};

static const char* const kIconStates[12] = {
    "###########..", "#..#####..##.", "#..#####..###", "#..#####..###", "#.........###", "#############",
    "#...........#", "#.#########.#", "#...........#", "#.#########.#", "#...........#", "#############",
};

static void CIcon(const SSurf* s, int32_t cx, int32_t cy, const char* const* rows, uint32_t color) {
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 13; x++) {
            if (rows[y][x] == '#') {
                FillRect(s, cx - 6 + x, cy - 6 + y, cx - 5 + x, cy - 5 + y, color);
            }
        }
    }
}

static void CStatus(const SSurf* s, int32_t ty) {
    const uint32_t frame = CC_DIM, dim = CC_FAINT;
    const int32_t cy = ty + CC_TAB_H / 2;
    /* Battery: 15x8 shell and a 2x4 nub, far right. */
    const int32_t bx = s->w - 21, by = cy - 4;
    FillRect(s, bx, by, bx + 15, by + 1, frame);
    FillRect(s, bx, by + 7, bx + 15, by + 8, frame);
    FillRect(s, bx, by, bx + 1, by + 8, frame);
    FillRect(s, bx + 14, by, bx + 15, by + 8, frame);
    FillRect(s, bx + 15, by + 2, bx + 17, by + 6, frame);
    uint8_t level = 0, charging = 0;
    if (Platform3DS_BatteryStatus(&level, &charging)) {
        int32_t w = level * 11 / 5;
        uint32_t fill = charging ? CC_GOOD : level <= 1 ? CC_BAD : level == 2 ? CC_WARN : CC_GOOD;
        if (w > 0) FillRect(s, bx + 2, by + 2, bx + 2 + w, by + 6, fill);
        if (charging) {
            /* A lightning bolt over the battery, dark edge so it reads on any fill. */
            static const uint8_t kBolt[8] = { 0x03, 0x06, 0x0C, 0x1E, 0x07, 0x06, 0x0C, 0x18 };
            for (int pass = 0; pass < 2; pass++) {
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 5; x++) {
                        if (!((kBolt[y] >> (4 - x)) & 1)) continue;
                        int32_t px = bx + 5 + x, py = by + y;
                        if (pass == 0) {
                            FillRect(s, px - 1, py, px + 2, py + 1, CC_BAR);
                            FillRect(s, px, py - 1, px + 1, py + 2, CC_BAR);
                        } else {
                            FillRect(s, px, py, px + 1, py + 1, CC_WARN);
                        }
                    }
                }
            }
        }
    } else {
        FillRect(s, bx + 5, by + 3, bx + 9, by + 5, dim);
    }
    /* Wi-Fi: three rising bars on the battery's baseline. */
    const int wifi = Platform3DS_WifiStrength();
    const int32_t wx = bx - 16;
    for (int i = 0; i < 3; i++) {
        int32_t h = 3 + i * 2;
        FillRect(s, wx + i * 4, cy + 4 - h, wx + i * 4 + 2, cy + 4, i < wifi ? frame : dim);
    }
    /* Clock, left of the bars. The console clock is local time. */
    char buf[8] = "--:--";
    time_t t = time(NULL);
    struct tm* tm = gmtime(&t);
    if (tm) snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    CText(s, wx - 5 - CTextWidth(buf), cy - 3, CC_TEXT, buf);
}

/* What the status icons last showed: the tab bar is repainted when they change
 * (Port_SecondScreen_3DS_NeedsPeriodicRefresh). */
static int sCompactChromeKey = -1;

static int CompactChromeKey(void) {
    time_t t = time(NULL);
    struct tm* tm = gmtime(&t);
    uint8_t level = 0, charging = 0;
    Platform3DS_BatteryStatus(&level, &charging);
    return (tm ? tm->tm_hour * 60 + tm->tm_min : 0) * 1000 + Platform3DS_WifiStrength() * 100 + level * 10 +
           (charging ? 1 : 0);
}

static int Compact_ChromeChanged(void) {
    return CompactChromeKey() != sCompactChromeKey;
}

static void Compact_PaintTabBar(const SSurf* s, TargetList* tl, int activeTab, int settingsPage) {
    static const char* const* const kIcons[6] = { kIconMap,    kIconItems,   kIconQuest,
                                                  kIconStates, kIconOptions, kIconDebug };
    static const uint8_t kTabs[6] = { SS_TAB_MAP,    SS_TAB_ITEMS,    SS_TAB_QUEST,
                                      SS_TAB_STATES, SS_TAB_SETTINGS, SS_TAB_DEBUG };
    int n = CompactTabCount();
    int32_t ty = CcTabY(s->h);
    FillRect(s, 0, ty, s->w, ty + CC_TAB_H, CC_BAR);
    FillRect(s, 0, sCc->tabsBottom ? ty : ty + CC_TAB_H - 1, s->w, sCc->tabsBottom ? ty + 1 : ty + CC_TAB_H,
             CC_TAB_EDGE);
    sCompactChromeKey = CompactChromeKey();
    CStatus(s, ty);

    (void)settingsPage;
    for (int i = 0; i < n; i++) {
        int32_t x = CC_TAB_X0 + i * CC_TAB_STEP;
        int on = activeTab == kTabs[i];
        CBox(s, x, ty + 2, CC_TAB_W, CC_TAB_H - 4, on ? CC_TAB_ON : CC_TAB, on ? CC_TAB_ON_EDGE : CC_TAB_EDGE);
        CIcon(s, x + CC_TAB_W / 2, ty + CC_TAB_H / 2, kIcons[i], on ? CC_TAB_ON_TEXT : CC_DIM);
        AddTarget(tl, (float)x, (float)ty, (float)(x + CC_TAB_W + 2), (float)(ty + CC_TAB_H), SS_ACT_TAB,
                  kTabs[i]);
    }
    /* The theme is picked in OPTIONS (THEME); the tab bar has no shortcut. */
}

/* The theme picked in the config, and its panel backdrop with it. */
static void Compact_ApplyThemeFromConfig(void) {
    Port_SecondScreen_3DS_SetUiTheme(Port_Config_GetUiTheme());
    /* The theme owns the panel backdrop; there is no separate option. */
    if (Port_Config_GetSecondScreenBackdrop() != sCc->backdrop) Port_Config_SetSecondScreenBackdrop(sCc->backdrop);
}

static void Compact_NextTheme(void) {
    int next = (int)(sCc - kCcThemes + 1) % CC_THEME_COUNT;
    Port_Config_SetUiTheme(next);
    Port_SecondScreen_3DS_SetUiTheme(next);
    Port_Config_SetSecondScreenBackdrop(sCc->backdrop);
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
        case SS_SET_FRAMES:
            return on ? CC_GOOD : (Port_Config_GetFramePacing() == PORT_3DS_PACING_NO_SKIP ? CC_WARN : CC_TEXT);
        default:
            return on ? CC_GOOD : CC_DIM;
    }
}

static int32_t CRowY(int row) {
    return CcContentTop() + 2 + row * (CC_CELL_H + CC_GAP);
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
    FillRect(s, 0, CcContentTop(), s->w, CcContentBottom(s->h), CC_BG);
    /* Screen */
    CSettingCell(s, tl, 0, 0, 1, "VIEW", SS_SET_ASPECT_RATIO);
    CSettingCell(s, tl, 0, 1, 1, "STYLE", SS_SET_DISPLAY_STYLE);
    CSettingCell(s, tl, 0, 2, 1, "FRAMES", SS_SET_FRAMES);
    CSettingCell(s, tl, 0, 3, 1, "TOP HUD", SS_SET_TOP_HUD);
    /* Game */
    CSettingCell(s, tl, 1, 0, 1, "TURBO", SS_SET_TURBO);
    CSettingCell(s, tl, 1, 1, 1, "VOLUME", SS_SET_VOLUME);
    CSettingCell(s, tl, 1, 2, 1, "AUTOSAVE", SS_SET_AUTOSAVE);
    CSettingCell(s, tl, 1, 3, 1, "HOLD TEXT", SS_SET_HOLD_ADVANCE);
    /* Map panel and colours */
    CSettingCell(s, tl, 2, 0, 1, "MAP FOLLOW", SS_SET_FOLLOW);
    CSettingCell(s, tl, 2, 1, 1, "CREST PINS", SS_SET_CRESTS);
    CSettingCell(s, tl, 2, 2, 1, "FLOOR BACK", SS_SET_FLOOR_RETURN);
    CSettingCell(s, tl, 2, 3, 1, "COLOURS", SS_SET_COLOR_CORRECTION);
    /* Look and system */
    {
        int32_t x, w;
        CColumn(s->w, 4, 0, 1, &x, &w);
        CCell(s, tl, x, CRowY(3), w, CC_CELL_H, "THEME", sCc->name, CC_ACCENT, SS_ACT_THEME, 0);
    }
    CSettingCell(s, tl, 3, 1, 1, "SHOW FPS", SS_SET_SHOW_FPS);
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
    int32_t y = CcContentBottom(s->h) - 3 * 10 - 2;
    CTextCentered(s, s->w / 2, y, CC_DIM, "MINISH CAP 3DS");
    snprintf(line, sizeof(line), "V%s", TMC_PORT_VERSION_LABEL);
    CTextCentered(s, s->w / 2, y + 10, CC_TEXT, line);
    snprintf(line, sizeof(line), "BASED ON ESTEBANPDN %s%s", TMC_UPSTREAM_VERSION,
             TMC3DS_DEBUG_TOOLS ? "  - DEBUG BUILD" : "");
    CTextCentered(s, s->w / 2, y + 20, TMC3DS_DEBUG_TOOLS ? CC_WARN : CC_FAINT, line);
}

/* ---- toast: one line over the bottom of the content for ~2 s ---- */

static char sToast[48];
static uint32_t sToastUntil;
static uint32_t sCompactTick; /* the tick of the frame being painted (PaintInto) */

static void Compact_Toast(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(sToast, sizeof(sToast), fmt, args);
    va_end(args);
    sToastUntil = sCompactTick + 40; /* paint ticks run at ~20 Hz */
}

static uint8_t sToastOnScreen;

#if TMC3DS_DEBUG_TOOLS
#include "ftp_server_3ds.h"
#endif

/* A notice that holds the toast while it lasts: a build arriving over FTP. */
static const char* Compact_StandingNotice(void) {
#if TMC3DS_DEBUG_TOOLS
    return FtpServer_Notice();
#else
    return NULL;
#endif
}

static int Compact_ToastVisible(void) {
    return sToast[0] && (int32_t)(sToastUntil - sCompactTick) > 0;
}

/* Paint while it shows, and once more to take it away. */
static int Compact_ToastNeedsPaint(void) {
    return Compact_StandingNotice() || Compact_ToastVisible() || sToastOnScreen;
}

static void Compact_PaintToast(const SSurf* s) {
    const char* notice = Compact_StandingNotice();
    if (notice) Compact_Toast("%s", notice);
    sToastOnScreen = (uint8_t)Compact_ToastVisible();
    if (!sToastOnScreen) return;
    int32_t w = CTextWidth(sToast) + 12, h = 15;
    int32_t x = (s->w - w) / 2, y = CcContentBottom(s->h) - h - 4;
    CBox(s, x, y, w, h, CC_BAR, CC_ACCENT);
    CTextCentered(s, s->w / 2, y + 4, CC_TEXT, sToast);
}

/* ---- STATES: save states (save_state_3ds.h), as in ../mzm and ../sm-3ds ----
 * A list of cards, newest first, three rows of two per page; a card opens its
 * detail: the screenshot, where and when, and LOAD / SAVE OVER / DELETE, each
 * needing a second tap. Taps run on the main thread, painting on the bottom
 * worker; the shared values below are single words. */

#include "save_state_3ds.h"

/* ---- busy notice: a window over everything while a save state job stops the game ---- */

static uint8_t sBusyOnScreen;

static const char* Compact_BusyText(void) {
    switch (SaveState_PendingJob()) {
        case SAVE_STATE_JOB_SAVE_NEW:
        case SAVE_STATE_JOB_OVERWRITE: return "SAVING STATE";
        case SAVE_STATE_JOB_LOAD: return "LOADING STATE";
        case SAVE_STATE_JOB_DELETE: return "DELETING STATE";
        default: return NULL;
    }
}

/* Paint while it shows, and once more to take it away. */
static int Compact_BusyNeedsPaint(void) {
    return Compact_BusyText() != NULL || sBusyOnScreen;
}

/* Last in the paint: it takes every tap (the target list is emptied) and draws over the panel. */
static void Compact_PaintBusy(const SSurf* s, TargetList* tl) {
    const char* text = Compact_BusyText();
    sBusyOnScreen = (uint8_t)(text != NULL);
    if (!text) return;
    tl->n = 0;
    const int32_t w = 156, h = 56;
    const int32_t x = (s->w - w) / 2, y = (s->h - h) / 2;
    FillRect(s, x - 2, y - 2, x + w + 2, y + h + 2, CC_BG);
    CBox(s, x, y, w, h, CC_BAR, CC_ACCENT);
    CTextCentered(s, s->w / 2, y + 14, CC_TEXT, text);
    CTextCentered(s, s->w / 2, y + 30, CC_DIM, "PLEASE WAIT");
    /* A row of dots that fills and empties, so a frozen screen can be told from a slow one. */
    const int lit = (int)((sCompactTick / 2u) % 6u);
    for (int i = 0; i < 5; ++i) {
        const int32_t dx = s->w / 2 - 20 + i * 10;
        FillRect(s, dx, y + 43, dx + 4, y + 47, i < lit ? CC_ACCENT : CC_TAB);
    }
}

enum {
    STATES_CMD_NEW = 1,
    STATES_CMD_PREV,
    STATES_CMD_NEXT,
    STATES_CMD_BACK,
    STATES_CMD_LOAD,
    STATES_CMD_OVER,
    STATES_CMD_DELETE,
    STATES_CMD_CARD0 = 16, /* + the card's place on the page */
    STATES_PER_PAGE = 6,
    STATES_CARD_THUMB_W = 80,
    STATES_CARD_THUMB_H = 48,
    STATES_ARM_TICKS = 60, /* ~3 s at the ~20 Hz paint rate */
};

static volatile int sStatesPage;
static volatile uint32_t sStatesOpenId;   /* 0 = the list */
static volatile int sStatesArmed;         /* the command waiting for its second tap */
static volatile uint32_t sStatesArmedAt;
static volatile uint8_t sStatesThumbPending;
static uint32_t sStatesToastGen;
static uint8_t sStatesScanAsked;

/* Screenshots: the open state's at full size, the page's cards scaled down.
 * Bottom worker only. */
static uint16_t sStatesBig[SAVE_STATE_THUMB_W * SAVE_STATE_THUMB_H];
static uint32_t sStatesBigId;
static uint8_t sStatesBigOk;
static struct {
    uint32_t id;
    uint8_t ok;
    uint16_t px[STATES_CARD_THUMB_W * STATES_CARD_THUMB_H];
} sStatesCards[STATES_PER_PAGE];
static uint32_t sStatesThumbGen = 0xFFFFFFFFu;

static uint32_t Rgb565ToColor(uint16_t v) {
    const uint32_t r = (v >> 11) & 31u, g = (v >> 5) & 63u, b = v & 31u;
    return RGB((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

static void BlitRgb565(const SSurf* s, int32_t x, int32_t y, const uint16_t* px, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) FillRect(s, x + i, y + j, x + i + 1, y + j + 1, Rgb565ToColor(px[j * w + i]));
}

static void Compact_StatesInvalidateThumbs(void) {
    const uint32_t gen = SaveState_Generation();
    if (gen == sStatesThumbGen) return;
    sStatesThumbGen = gen; /* an overwrite replaces a picture under the same id */
    sStatesBigId = 0;
    for (int i = 0; i < STATES_PER_PAGE; ++i) sStatesCards[i].id = 0;
}

static const uint16_t* Compact_StatesCardThumb(int slot, uint32_t id, int* loads) {
    if (sStatesCards[slot].id == id) return sStatesCards[slot].ok ? sStatesCards[slot].px : NULL;
    if (*loads <= 0) {
        sStatesThumbPending = 1; /* a later paint reads it: one SD read per paint */
        return NULL;
    }
    --*loads;
    sStatesCards[slot].id = id;
    sStatesCards[slot].ok = SaveState_ReadThumb(id, sStatesBig) ? 1 : 0;
    sStatesBigId = 0; /* sStatesBig was borrowed */
    if (sStatesCards[slot].ok) {
        for (int y = 0; y < STATES_CARD_THUMB_H; ++y)
            for (int x = 0; x < STATES_CARD_THUMB_W; ++x)
                sStatesCards[slot].px[y * STATES_CARD_THUMB_W + x] =
                    sStatesBig[(y * SAVE_STATE_THUMB_H / STATES_CARD_THUMB_H) * SAVE_STATE_THUMB_W +
                               x * SAVE_STATE_THUMB_W / STATES_CARD_THUMB_W];
    }
    return sStatesCards[slot].ok ? sStatesCards[slot].px : NULL;
}

static void Compact_StatesWhen(uint32_t savedAt, char* out, size_t cap) {
    const time_t t = (time_t)savedAt;
    const struct tm* tm = savedAt ? gmtime(&t) : NULL;
    if (tm) snprintf(out, cap, "%02d/%02d %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
    else snprintf(out, cap, "--");
}

static const char* Compact_StatesArea(unsigned area, char* buf, size_t cap) {
    const char* name = SaveState_AreaName(area);
    if (!name) {
        snprintf(buf, cap, "AREA %02X", area);
        return buf;
    }
    return name;
}

static bool Compact_StatesArmed(int cmd) {
    return sStatesArmed == cmd && (int32_t)(sCompactTick - sStatesArmedAt) < STATES_ARM_TICKS;
}

static void Compact_PaintStatesList(const SSurf* s, TargetList* tl) {
    const int32_t top = CcContentTop() + 2, bottom = CcContentBottom(s->h) - CC_MARGIN;
    const int count = SaveState_Count();
    const int pages = count > 0 ? (count + STATES_PER_PAGE - 1) / STATES_PER_PAGE : 1;
    int page = sStatesPage;
    if (page >= pages) page = sStatesPage = pages - 1;
    if (page < 0) page = sStatesPage = 0;

    const bool canSave = SaveState_CanSave() && !SaveState_Busy();
    CButton(s, tl, CC_MARGIN, top, 96, 20, "+ NEW STATE", canSave ? CC_ACCENT : CC_FAINT, SS_ACT_STATES,
            STATES_CMD_NEW);
    char line[40];
    snprintf(line, sizeof(line), "%d STATE%s  %d/%d", count, count == 1 ? "" : "S", page + 1, pages);
    CText(s, CC_MARGIN + 104, top + 7, CC_DIM, line);
    CButton(s, tl, s->w - CC_MARGIN - 62, top, 30, 20, "<", page > 0 ? CC_TEXT : CC_FAINT, SS_ACT_STATES,
            STATES_CMD_PREV);
    CButton(s, tl, s->w - CC_MARGIN - 30, top, 30, 20, ">", page + 1 < pages ? CC_TEXT : CC_FAINT, SS_ACT_STATES,
            STATES_CMD_NEXT);

    if (count == 0) {
        CTextCentered(s, s->w / 2, (top + bottom) / 2 - 8, CC_DIM, "NO SAVE STATES YET");
        CTextCentered(s, s->w / 2, (top + bottom) / 2 + 4, CC_FAINT,
                      SaveState_CanSave() ? "+ NEW STATE SAVES ONE" : "LOAD A GAME TO SAVE ONE");
        return;
    }
    const int32_t gridTop = top + 24;
    const int32_t cardH = (bottom - gridTop - 2 * CC_GAP) / 3;
    int loads = 1;
    for (int k = 0; k < STATES_PER_PAGE; ++k) {
        SaveStateInfo info;
        if (!SaveState_InfoAt(page * STATES_PER_PAGE + k, &info)) break;
        int32_t x, w;
        CColumn(s->w, 2, k % 2, 1, &x, &w);
        const int32_t y = gridTop + (k / 2) * (cardH + CC_GAP);
        CBox(s, x, y, w, cardH, CC_CELL, CC_CELL_EDGE);
        const int32_t ty = y + (cardH - STATES_CARD_THUMB_H) / 2;
        const uint16_t* thumb = info.hasThumb ? Compact_StatesCardThumb(k, info.id, &loads) : NULL;
        if (thumb) BlitRgb565(s, x + 3, ty, thumb, STATES_CARD_THUMB_W, STATES_CARD_THUMB_H);
        else FillRect(s, x + 3, ty, x + 3 + STATES_CARD_THUMB_W, ty + STATES_CARD_THUMB_H, CC_BG);
        const int32_t tx = x + STATES_CARD_THUMB_W + 7;
        char when[16], area[16], buf[24];
        Compact_StatesWhen(info.savedAt, when, sizeof(when));
        snprintf(buf, sizeof(buf), "#%d", page * STATES_PER_PAGE + k + 1);
        CText(s, tx, y + 5, CC_ACCENT, buf);
        snprintf(area, sizeof(area), "%.11s", Compact_StatesArea(info.area, buf, sizeof(buf)));
        CText(s, tx, y + 16, info.compatible ? CC_TEXT : CC_BAD, area);
        snprintf(buf, sizeof(buf), "HP %u/%u", info.health / 8u, info.maxHealth / 8u);
        CText(s, tx, y + 27, CC_DIM, buf);
        CText(s, tx, y + 38, CC_FAINT, when + 6); /* the time; the date is in the detail */
        AddTarget(tl, (float)x, (float)y, (float)(x + w), (float)(y + cardH), SS_ACT_STATES,
                  (uint8_t)(STATES_CMD_CARD0 + k));
    }
}

static void Compact_PaintStatesDetail(const SSurf* s, TargetList* tl, uint32_t id) {
    SaveStateInfo info;
    int number = 0;
    for (int i = 0; i < SaveState_Count(); ++i) {
        if (SaveState_InfoAt(i, &info) && info.id == id) {
            number = i + 1;
            break;
        }
    }
    if (!number) { /* deleted meanwhile */
        sStatesOpenId = 0;
        Compact_PaintStatesList(s, tl);
        return;
    }
    const int32_t top = CcContentTop() + 2;
    if (sStatesBigId != id) {
        sStatesBigOk = info.hasThumb && SaveState_ReadThumb(id, sStatesBig);
        sStatesBigId = id;
    }
    CBox(s, CC_MARGIN, top, SAVE_STATE_THUMB_W + 2, SAVE_STATE_THUMB_H + 2, CC_CELL, CC_CELL_EDGE);
    if (sStatesBigOk) BlitRgb565(s, CC_MARGIN + 1, top + 1, sStatesBig, SAVE_STATE_THUMB_W, SAVE_STATE_THUMB_H);
    else CTextCentered(s, CC_MARGIN + 1 + SAVE_STATE_THUMB_W / 2, top + 56, CC_FAINT, "NO PICTURE");

    const int32_t tx = CC_MARGIN + SAVE_STATE_THUMB_W + 8;
    int32_t ty = top + 2;
    char buf[40], area[24];
    snprintf(buf, sizeof(buf), "STATE %d", number);
    CText(s, tx, ty, CC_ACCENT, buf);
    ty += 13;
    const char* name = Compact_StatesArea(info.area, area, sizeof(area));
    snprintf(buf, sizeof(buf), "%.17s", name);
    CText(s, tx, ty, CC_TEXT, buf);
    if (strlen(name) > 17) {
        ty += 10;
        snprintf(buf, sizeof(buf), "%.17s", name + 17);
        CText(s, tx, ty, CC_TEXT, buf);
    }
    ty += 11;
    snprintf(buf, sizeof(buf), "ROOM %02X", info.room);
    CText(s, tx, ty, CC_DIM, buf);
    ty += 11;
    snprintf(buf, sizeof(buf), "HP %u/%u  R %u", info.health / 8u, info.maxHealth / 8u, info.rupees);
    CText(s, tx, ty, CC_DIM, buf);
    ty += 11;
    Compact_StatesWhen(info.savedAt, buf, sizeof(buf));
    CText(s, tx, ty, CC_DIM, buf);
    ty += 11;
    snprintf(buf, sizeof(buf), "%.17s", info.build);
    CText(s, tx, ty, CC_FAINT, buf);
    ty += 13;
    if (!info.compatible) {
        CText(s, tx, ty, CC_BAD, "ANOTHER BUILD:");
        CText(s, tx, ty + 10, CC_BAD, "CANNOT LOAD");
    }

    const int32_t by = top + SAVE_STATE_THUMB_H + 8;
    static const char* const kLabels[4] = { "LOAD", "SAVE OVER", "DELETE", "BACK" };
    static const uint8_t kCmds[4] = { STATES_CMD_LOAD, STATES_CMD_OVER, STATES_CMD_DELETE, STATES_CMD_BACK };
    const bool busy = SaveState_Busy();
    for (int i = 0; i < 4; ++i) {
        int32_t x, w;
        CColumn(s->w, 4, i, 1, &x, &w);
        bool enabled = !busy;
        if (kCmds[i] == STATES_CMD_LOAD) enabled = enabled && info.compatible;
        if (kCmds[i] == STATES_CMD_OVER) enabled = enabled && SaveState_CanSave();
        if (kCmds[i] == STATES_CMD_BACK) enabled = true;
        const bool armed = Compact_StatesArmed(kCmds[i]);
        CButton(s, tl, x, by, w, 24, armed ? "SURE?" : kLabels[i], armed ? CC_WARN : (enabled ? CC_TEXT : CC_FAINT),
                SS_ACT_STATES, kCmds[i]);
    }
    CText(s, CC_MARGIN + 1, by + 32, CC_FAINT, "TAP TWICE TO CONFIRM");
}

static void Compact_PaintStates(const SSurf* s, TargetList* tl) {
    FillRect(s, 0, CcContentTop(), s->w, CcContentBottom(s->h), CC_BG);
    if (!sStatesScanAsked && SaveState_Request(SAVE_STATE_JOB_SCAN, 0)) sStatesScanAsked = 1;
    Compact_StatesInvalidateThumbs();
    sStatesThumbPending = 0;
    /* The result of a save, load or delete, once. */
    const uint32_t gen = SaveState_Generation();
    if (gen != sStatesToastGen) {
        sStatesToastGen = gen;
        char message[48];
        SaveState_LastResult(message, sizeof(message), NULL);
        if (strcmp(message, "STATES READ")) Compact_Toast("%s", message);
    }
    if (SaveState_Busy()) Compact_Toast("PLEASE WAIT");
    const uint32_t id = sStatesOpenId;
    if (id) Compact_PaintStatesDetail(s, tl, id);
    else Compact_PaintStatesList(s, tl);
}

/* Repaint while a job runs, a result is due or a picture is still to read. */
static int Compact_StatesNeedsPaint(void) {
    return SaveState_Busy() || SaveState_Generation() != sStatesToastGen || sStatesThumbPending ||
           (sStatesArmed && (int32_t)(sCompactTick - sStatesArmedAt) < STATES_ARM_TICKS + 2);
}

/* SS_ACT_STATES taps (main thread). */
static void Compact_StatesCommand(int cmd) {
    const uint32_t id = sStatesOpenId;
    if (cmd >= STATES_CMD_CARD0 && cmd < STATES_CMD_CARD0 + STATES_PER_PAGE) {
        SaveStateInfo info;
        if (SaveState_InfoAt(sStatesPage * STATES_PER_PAGE + (cmd - STATES_CMD_CARD0), &info)) sStatesOpenId = info.id;
        sStatesArmed = 0;
        return;
    }
    switch (cmd) {
        case STATES_CMD_NEW:
            if (!SaveState_CanSave()) Compact_Toast("LOAD A GAME FIRST");
            else if (SaveState_Request(SAVE_STATE_JOB_SAVE_NEW, 0)) sStatesPage = 0;
            return;
        case STATES_CMD_PREV:
            if (sStatesPage > 0) --sStatesPage;
            return;
        case STATES_CMD_NEXT: ++sStatesPage; return;
        case STATES_CMD_BACK:
            sStatesOpenId = 0;
            sStatesArmed = 0;
            return;
        default: break;
    }
    /* LOAD, SAVE OVER, DELETE: the first tap arms, the second does it. */
    if (!Compact_StatesArmed(cmd)) {
        sStatesArmed = cmd;
        sStatesArmedAt = sCompactTick;
        return;
    }
    sStatesArmed = 0;
    if (cmd == STATES_CMD_LOAD) SaveState_Request(SAVE_STATE_JOB_LOAD, id);
    else if (cmd == STATES_CMD_OVER) SaveState_Request(SAVE_STATE_JOB_OVERWRITE, id);
    else if (cmd == STATES_CMD_DELETE && SaveState_Request(SAVE_STATE_JOB_DELETE, id)) sStatesOpenId = 0;
}

/* ---- MAP / ITEMS / QUEST without a game ---- */

static void Compact_PaintNoGame(const SSurf* s) {
    const int32_t top = CcContentTop(), bottom = CcContentBottom(s->h);
    FillRect(s, 0, top, s->w, bottom, CC_BG);
    const int32_t y = (top + bottom) / 2 - kUiGlyphH;
    CTextCentered(s, s->w / 2, y, CC_TEXT, "AVAILABLE ONCE A GAME IS LOADED");
    CTextCentered(s, s->w / 2, y + kUiGlyphH + 6, CC_DIM, "OPTIONS AND DEBUG WORK NOW");
}

/* ---- DEBUG (debug-tools builds) ---- */

#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#include "debug_perf_3ds.h"
#include "debug_switches_3ds.h"
#include "port_ppu_gpu_3ds.h"
#include "ftp_server_3ds.h"

enum { DBG_PAGE_MAIN, DBG_PAGE_SWITCHES };
static int sDebugPage;

/* SS_ACT_DEBUG arguments. */
enum {
    DBG_CMD_PAGE_MAIN = 1,
    DBG_CMD_PAGE_SWITCHES,
    DBG_CMD_LOG_RUN,
    DBG_CMD_LOG_MODE,
    DBG_CMD_LOG_MARK,
    DBG_CMD_PERF_RUN,
    DBG_CMD_GPU_CHECK,
    DBG_CMD_FTP_RUN,
    DBG_CMD_SWITCH0 = 32, /* + DEBUG_SW_* */
};

/* A cell for something that runs (mzm, sm-3ds): the square button on its right
 * starts it (green triangle) or stops it (red square); the rest of the cell
 * changes its option, or does nothing when `optionCmd` is 0. */
static void CRunCell(const SSurf* s, TargetList* tl, int32_t x, int32_t y, int32_t w, const char* name,
                     const char* value, uint32_t valueColor, int running, uint8_t runCmd, uint8_t optionCmd) {
    const int32_t bw = 18;
    CBox(s, x, y, w, CC_CELL_H, CC_CELL, CC_CELL_EDGE);
    CText(s, x + 4, y + 4, CC_DIM, name);
    CText(s, x + 4, y + 14, valueColor, value);
    int32_t bx = x + w - bw - 2, by = y + 2, bh = CC_CELL_H - 4;
    CBox(s, bx, by, bw, bh, running ? CC_BAR : CC_BTN, running ? CC_BAD : CC_BTN_EDGE);
    int32_t cx = bx + bw / 2, cy = by + bh / 2;
    if (running) {
        FillRect(s, cx - 3, cy - 3, cx + 4, cy + 4, CC_BAD);
    } else {
        for (int i = 0; i < 7; i++) FillRect(s, cx - 3 + i, cy - 3 + i / 2, cx - 2 + i, cy + 4 - i / 2, CC_GOOD);
    }
    AddTarget(tl, (float)bx, (float)y, (float)(x + w), (float)(y + CC_CELL_H), SS_ACT_DEBUG, runCmd);
    if (optionCmd) AddTarget(tl, (float)x, (float)y, (float)bx, (float)(y + CC_CELL_H), SS_ACT_DEBUG, optionCmd);
}

static void CSectionTitle(const SSurf* s, int32_t y, const char* title) {
    CText(s, CC_MARGIN + 1, y, CC_FAINT, title);
}

static void Compact_PaintDebugMain(const SSurf* s, TargetList* tl, const SecondScreenSnapshot* snap, uint32_t tick,
                                   uint32_t dumpFlashUntil, uint32_t loadStateFlashUntil, int loadStateResult) {
    char v[8][24];
    double now = Port_PPU_3DS_CurrentFps(), avg = Port_PPU_3DS_AverageFps();
    snprintf(v[0], sizeof(v[0]), "%s", Platform3DS_IsNew3DS() ? "NEW 3DS" : "OLD 3DS");
    snprintf(v[1], sizeof(v[1]), "%.1f", now);
    snprintf(v[2], sizeof(v[2]), "%.1f", avg);
    snprintf(v[3], sizeof(v[3]), "%u%%", Platform3DS_Core1TimeLimit());
    snprintf(v[4], sizeof(v[4]), "%s", Port_Config_WidescreenEnabled() ? "WIDE" : "NATIVE");
    snprintf(v[5], sizeof(v[5]), "%02X / %02X", snap->area, snap->room);
    snprintf(v[6], sizeof(v[6]), "%d,%d", (int)snap->playerX, (int)snap->playerY);
    static const char* const kNames[7] = { "CONSOLE", "FPS NOW", "FPS AVG", "CORE 1", "VIEW", "AREA/ROOM",
                                           "LINK X,Y" };
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
    {
        int32_t x, w;
        CColumn(s->w, 4, 3, 1, &x, &w);
        CCell(s, tl, x, CRowY(1), w, CC_CELL_H, "SWITCHES", "PERF >", CC_ACCENT, SS_ACT_DEBUG,
              DBG_CMD_PAGE_SWITCHES);
    }

    int32_t y = CRowY(2) + 2;
    CSectionTitle(s, y, "RECORD");
    y += 10;
    char value[24];
    int32_t x, w;
    /* SD log: side button runs it, the rest switches BUFFERED / DIRECT. */
    if (DebugLog_Running()) snprintf(value, sizeof(value), "%04d %s", DebugLog_FileNumber(), DebugLog_Direct() ? "DIR" : "BUF");
    else snprintf(value, sizeof(value), "%s", DebugLog_Direct() ? "DIRECT" : "BUFFERED");
    CColumn(s->w, 4, 0, 2, &x, &w);
    CRunCell(s, tl, x, y, w, "SD LOG", value, DebugLog_Running() ? CC_GOOD : CC_DIM, DebugLog_Running(),
             DBG_CMD_LOG_RUN, DBG_CMD_LOG_MODE);
    CColumn(s->w, 4, 2, 2, &x, &w);
    if (DebugPerf_Running())
        snprintf(value, sizeof(value), "%u ROWS %u%%", DebugPerf_Rows(), DebugPerf_Rows() * 100u / DebugPerf_Capacity());
    else
        snprintf(value, sizeof(value), "STOPPED");
    CRunCell(s, tl, x, y, w, "PERF RECORDER", value, DebugPerf_Running() ? CC_GOOD : CC_DIM, DebugPerf_Running(),
             DBG_CMD_PERF_RUN, 0);

    y += CC_CELL_H + CC_GAP + 2;
    CSectionTitle(s, y, "TOOLS");
    y += 10;
    CColumn(s->w, 4, 0, 1, &x, &w);
    CCell(s, tl, x, y, w, CC_CELL_H, "LOG MARK", DebugLog_Running() ? "WRITE" : "LOG OFF",
          DebugLog_Running() ? CC_ACCENT : CC_FAINT, SS_ACT_DEBUG, DBG_CMD_LOG_MARK);
    /* SCREEN DUMP: both screens, the memories and the frame's state as numbered
     * debug/tmc-dump-NNNN-* files, written at the next frame boundary (as
     * ../sm-3ds and ../mzm do). It replaced upstream's L+R+A and its LOAD STATE;
     * the STATES tab restores a game state. */
    const int dumped = (int32_t)(dumpFlashUntil - tick) > 0;
    (void)loadStateFlashUntil;
    (void)loadStateResult;
    CColumn(s->w, 4, 1, 2, &x, &w);
    CCell(s, tl, x, y, w, CC_CELL_H, "SCREEN DUMP", dumped ? "QUEUED" : "TAKE", dumped ? CC_GOOD : CC_ACCENT,
          SS_ACT_DEVELOPER_DUMP, 0);
    CSettingCellAt(s, tl, y, 3, 1, "SHOW FPS", SS_SET_SHOW_FPS);

    /* GPU CHECK: the next frame drawn by both renderers and compared; a frame
     * that differs is saved as debug/tmc-check-NNNN-* (a screen dump takes the
     * check too, into its own set). */
    y += CC_CELL_H + CC_GAP;
    PortPpuGpu3DSStats gpu;
    PortPpuGpu3DS_GetStats(&gpu);
    CColumn(s->w, 4, 0, 2, &x, &w);
    int32_t gpuX = x, gpuW = w;
    /* FTP: the SD card over the network while the game runs (ftp_server_3ds.h). */
    {
        unsigned sent = 0, received = 0;
        bool connected = false;
        FtpServer_Stats(&sent, &received, &connected);
        char address[40];
        FtpServer_Address(address, sizeof(address));
        CColumn(s->w, 4, 2, 2, &x, &w);
        CRunCell(s, tl, x, y, w, connected ? "FTP  CLIENT ON" : "FTP SERVER", address,
                 FtpServer_Running() ? CC_GOOD : CC_DIM, FtpServer_Running(), DBG_CMD_FTP_RUN, 0);
        (void)sent;
        (void)received;
    }
    x = gpuX;
    w = gpuW;
    uint32_t color = CC_ACCENT;
    if (!gpu.initialized) {
        snprintf(value, sizeof(value), "NO GPU RENDERER");
        color = CC_FAINT;
    } else if (gpu.disabled) {
        snprintf(value, sizeof(value), "GPU OFF: FAILED");
        color = CC_BAD;
    } else if (gpu.parityChecks) {
        snprintf(value, sizeof(value), "%llu DONE, %llu PX DIFF", (unsigned long long)gpu.parityChecks,
                 (unsigned long long)gpu.differingPixels);
        color = gpu.differingPixels ? CC_WARN : CC_GOOD;
    } else {
        snprintf(value, sizeof(value), "COMPARE NEXT FRAME");
    }
    CCell(s, tl, x, y, w, CC_CELL_H, "GPU CHECK", value, color, gpu.initialized ? SS_ACT_DEBUG : 0,
          DBG_CMD_GPU_CHECK);
}

static void Compact_PaintDebugSwitches(const SSurf* s, TargetList* tl) {
    int32_t x, w;
    CColumn(s->w, 4, 0, 1, &x, &w);
    CButton(s, tl, x, CRowY(0), w, 16, "< BACK", CC_TEXT, SS_ACT_DEBUG, DBG_CMD_PAGE_MAIN);
    CText(s, x + w + 8, CRowY(0) + 5, CC_DIM, "TAP TO CHANGE, APPLIES AT ONCE");
    int32_t y0 = CRowY(0) + 16 + CC_GAP + 2;
    for (int id = 0; id < DEBUG_SW_COUNT; id++) {
        char value[24];
        DebugSwitch_Value(id, value, sizeof(value));
        CColumn(s->w, 4, id % 4, 1, &x, &w);
        uint32_t color = DebugSwitch_IsDefault(id) ? CC_TEXT : CC_ACCENT;
        CCell(s, tl, x, y0 + (id / 4) * (CC_CELL_H + CC_GAP), w, CC_CELL_H, DebugSwitch_Name(id), value, color,
              SS_ACT_DEBUG, (uint8_t)(DBG_CMD_SWITCH0 + id));
    }
    int32_t y = y0 + ((DEBUG_SW_COUNT + 3) / 4) * (CC_CELL_H + CC_GAP) + 2;
    CText(s, CC_MARGIN + 1, y, CC_FAINT, "WHITE: DEFAULT  BLUE: CHANGED  (TMC3DS.INI)");
}

static void Compact_PaintDebug(const SSurf* s, TargetList* tl, const SecondScreenSnapshot* snap, uint32_t tick,
                               uint32_t dumpFlashUntil, uint32_t loadStateFlashUntil, int loadStateResult) {
    FillRect(s, 0, CcContentTop(), s->w, CcContentBottom(s->h), CC_BG);
    if (sDebugPage == DBG_PAGE_SWITCHES) {
        Compact_PaintDebugSwitches(s, tl);
    } else {
        Compact_PaintDebugMain(s, tl, snap, tick, dumpFlashUntil, loadStateFlashUntil, loadStateResult);
    }
}

/* SS_ACT_DEBUG taps (main thread, from Port_SecondScreen_OnTap). */
static void Compact_DebugCommand(int cmd) {
    if (cmd >= DBG_CMD_SWITCH0 && cmd < DBG_CMD_SWITCH0 + DEBUG_SW_COUNT) {
        int id = cmd - DBG_CMD_SWITCH0;
        DebugSwitch_Cycle(id);
        char value[24];
        DebugSwitch_Value(id, value, sizeof(value));
        DebugLog_Printf("switch %s -> %s", DebugSwitch_Name(id), value);
        return;
    }
    switch (cmd) {
        case DBG_CMD_PAGE_MAIN: sDebugPage = DBG_PAGE_MAIN; break;
        case DBG_CMD_PAGE_SWITCHES: sDebugPage = DBG_PAGE_SWITCHES; break;
        case DBG_CMD_LOG_RUN:
            if (DebugLog_Running()) {
                int n = DebugLog_FileNumber();
                DebugLog_Stop();
                Port_Config_SetDebugLogOn(false);
                Compact_Toast("LOG %04d SAVED", n);
            } else {
                Port_Config_SetDebugLogOn(true);
                DebugLog_Start();
                if (DebugLog_Running()) Compact_Toast("LOG %04d STARTED", DebugLog_FileNumber());
                else Compact_Toast("CANNOT WRITE THE LOG");
            }
            break;
        case DBG_CMD_LOG_MODE:
            DebugLog_SetDirect(!DebugLog_Direct());
            Port_Config_SetDebugLogDirect(DebugLog_Direct());
            break;
        case DBG_CMD_LOG_MARK:
            if (DebugLog_Running()) Compact_Toast("MARK %d", DebugLog_Mark());
            break;
        case DBG_CMD_PERF_RUN:
            if (DebugPerf_Running()) {
                unsigned rows = DebugPerf_Rows();
                int n = DebugPerf_Stop();
                if (n > 0) Compact_Toast("PERF %04d: %u ROWS", n, rows);
                else Compact_Toast("CANNOT WRITE THE CSV");
            } else if (DebugPerf_Start()) {
                Compact_Toast("RECORDING");
            } else {
                Compact_Toast("NO MEMORY");
            }
            break;
        case DBG_CMD_FTP_RUN:
            if (FtpServer_Running() || Port_Config_DebugFtpOn()) {
                /* Also cancels a restore still waiting for Wi-Fi. */
                FtpServer_Stop();
                Port_Config_SetDebugFtpOn(false);
                Compact_Toast("FTP STOPPED");
            } else if (Port_Config_SetDebugFtpOn(true), FtpServer_Start()) {
                char address[40];
                FtpServer_Address(address, sizeof(address));
                Compact_Toast("FTP %s", address);
            } else {
                char reason[40];
                FtpServer_Address(reason, sizeof(reason));
                Compact_Toast("FTP: %s", reason);
            }
            break;
        case DBG_CMD_GPU_CHECK:
            Port_PPU_3DS_RequestGpuCheck();
            DebugLog_Printf("gpu check requested");
            Compact_Toast("GPU CHECK REQUESTED");
            break;
        default: break;
    }
}
#endif

/* ---- modal: a titled box over the content with one or two buttons ---- */

/* Dims what is behind, draws the box and its lines, and replaces the frame's
 * tap targets with its own (a modal takes every tap). `right` may be NULL. */
static void Compact_PaintModal(const SSurf* s, TargetList* tl, const char* title, const char* const* lines,
                               int nLines, const char* left, uint8_t leftAction, const char* right,
                               uint8_t rightAction) {
    tl->n = 0;
    for (int32_t y = 0; y < s->h; y += 2) FillRect(s, 0, y, s->w, y + 1, CC_BG);
    const int32_t w = 260, h = 30 + nLines * 10 + 36;
    const int32_t x = (s->w - w) / 2, y = (s->h - h) / 2;
    CBox(s, x, y, w, h, CC_CELL, CC_CELL_EDGE);
    CTextCentered(s, s->w / 2, y + 8, CC_ACCENT, title);
    for (int i = 0; i < nLines; ++i) CTextCentered(s, s->w / 2, y + 24 + i * 10, CC_TEXT, lines[i]);
    const int32_t by = y + h - 28, bw = right ? (w - 12 - CC_GAP) / 2 : w - 8;
    CButton(s, tl, x + 4, by, bw, 24, left, CC_TEXT, leftAction, 0);
    if (right) CButton(s, tl, x + 8 + bw + CC_GAP - 4, by, bw, 24, right, CC_WARN, rightAction, 0);
}

/* ---- sidebar, ITEMS, QUEST and the map's chip in the same style ----
 * The game's art that stays is sprites: item icons, hearts, the wallet, the
 * map itself. Every frame, button and cell is drawn here. */

/* A button over the map or a list: label centred in a theme box. */
static void Compact_MapChip(const SSurf* s, const char* label, float cx, float cyBottom, float* out) {
    const int32_t w = CTextWidth(label) + 16, h = 18;
    const int32_t x = (int32_t)cx - w / 2, y = (int32_t)cyBottom - h;
    CBox(s, x, y, w, h, CC_BTN, CC_BTN_EDGE);
    CTextCentered(s, x + w / 2, y + (h - kUiGlyphH) / 2, CC_TEXT, label);
    out[0] = (float)x;
    out[1] = (float)y;
    out[2] = (float)(x + w);
    out[3] = (float)(y + h);
}

/* The A/B cell: its letter, the equipped item's icon and its ammo; tapping it
 * arms it for the next item tapped. */
static void Compact_EquipCell(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, int32_t x,
                              int32_t y, int32_t w, int32_t h, int isA, int armed, uint32_t tick) {
    const uint32_t pulse = armed ? ((tick / 12u) & 1u ? CC_WARN : CC_ACCENT) : CC_CELL_EDGE;
    CBox(s, x, y, w, h, CC_CELL, pulse);
    if (armed) FillRect(s, x + 1, y + 1, x + w - 1, y + h - 1, MixColor(CC_CELL, CC_WARN, 40));
    CText(s, x + 4, y + 4, CC_DIM, isA ? "A" : "B");
    uint8_t itemId = isA ? snap->equippedA : snap->equippedB;
    if (itemId >= ITEMID_BOTTLE1 && itemId < ITEMID_BOTTLE1 + 4) {
        const uint8_t content = snap->bottleContents[itemId - ITEMID_BOTTLE1];
        if (content != 0) itemId = content;
    }
    if (itemId != 0) {
        const int32_t ix = x + w - 36, iy = y + 3;
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, ix, iy, 2, itemId);
        DrawItemAmmoCount(s, snap, itemId, x + 4, y + h - 11, 1);
    }
    AddTarget(tl, (float)x, (float)y, (float)(x + w), (float)(y + h), SS_ACT_RING, (uint8_t)(isA ? 1 : 2));
}

static void Compact_PaintSidebar(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, int32_t x,
                                 int32_t y, int32_t w, int32_t h, uint32_t tick, int armedRing) {
    FillRect(s, x - 1, y, x, y + h, CC_CELL_EDGE);
    const int32_t ix = x + 3, iw = w - 6;
    int32_t cy = y + 2;

    /* Hearts, eight to a row, the game's own sprites. */
    int maxHearts = snap->maxHealth / 8;
    if (maxHearts > 20) maxHearts = 20;
    if (maxHearts < 1) maxHearts = 1;
    const int cols = iw / 8 < 8 ? iw / 8 : 8;
    const int quarters = snap->health == 1 ? 1 : snap->health / 2;
    const int full = quarters / 4, frac = quarters & 3;
    for (int i = 0; i < maxHearts; i++) {
        int state = SST_HEART_EMPTY;
        if (i < full) state = SST_HEART_FULL;
        else if (i == full && frac != 0) state = SST_HEART_Q1 + (frac - 1);
        const SecondScreenThemeSprite* hs = Port_SecondScreenTheme_Get(state);
        if (hs) BlitSprite(s, hs, ix + (i % cols) * 8, cy + (i / cols) * 8, 1);
    }
    cy += ((maxHearts + cols - 1) / cols) * 8 + 4;

    /* The charge meter while the HUD is hidden, else the R prompt. */
    if (Port_SecondScreenChargeVisible(snap)) {
        const int steps = Port_SecondScreenChargeSteps(snap);
        const int32_t cell = (iw - 9) / 10;
        for (int c = 0; c < 10; c++) {
            int q = steps - c * 4;
            q = q < 0 ? 0 : q > 4 ? 4 : q;
            const int32_t cx = ix + c * (cell + 1);
            FillRect(s, cx, cy, cx + cell, cy + 8, CC_CELL_EDGE);
            FillRect(s, cx + 1, cy + 1, cx + cell - 1, cy + 7, CC_BAR);
            if (q) FillRect(s, cx + 1, cy + 1, cx + 1 + (cell - 2) * q / 4, cy + 7, CC_WARN);
        }
    } else if (snap->rActionFrame != 0) {
        /* Words of ActionRButton (include/player.h). */
        static const char* const kWords[] = { NULL,    "CANCEL", "DROP", "THROW", "READ", "CHECK", "OPEN",
                                              "SPEAK", "GRAB",   "LIFT", "GROW",  "SHRINK", "ROLL" };
        const char* word = snap->rActionId < (uint8_t)(sizeof(kWords) / sizeof(kWords[0]))
                               ? kWords[snap->rActionId] : NULL;
        char line[24];
        snprintf(line, sizeof(line), "R %s", word ? word : "");
        CBox(s, ix, cy, iw, 14, CC_BTN, CC_BTN_EDGE);
        CTextCentered(s, ix + iw / 2, cy + 4, CC_TEXT, line);
    }
    cy += 18;

    /* A and B, stacked. */
    const int32_t cellH = 44;
    Compact_EquipCell(s, snap, tl, ix, cy, iw, cellH, 1, armedRing == 1, tick);
    Compact_EquipCell(s, snap, tl, ix, cy + cellH + 3, iw, cellH, 0, armedRing == 2, tick);

    /* Rupees, and keys in a dungeon, along the bottom. */
    const int dungeon = (snap->areaFlags & SECOND_SCREEN_AR_HAS_KEYS) != 0;
    int32_t by = y + h - (dungeon ? 34 : 17);
    char num[16];
    const SecondScreenThemeSprite* wallet = Port_SecondScreenTheme_Get(SST_RUPEE_WALLET0 + (snap->walletType & 3));
    if (wallet) BlitSprite(s, wallet, ix, by, 1);
    snprintf(num, sizeof(num), "%u", (unsigned)snap->rupees);
    CText(s, ix + 20, by + 5, snap->walletMax != 0 && snap->rupees >= snap->walletMax ? CC_GOOD : CC_TEXT, num);
    if (dungeon) {
        const SecondScreenThemeSprite* key = Port_SecondScreenTheme_Get(SST_KEY);
        if (key) BlitSprite(s, key, ix, by + 17, 1);
        snprintf(num, sizeof(num), "%u", (unsigned)snap->dungeonKeys);
        CText(s, ix + 20, by + 22, CC_TEXT, num);
    }
}

/* A square cell for an item slot. */
static void Compact_ItemCell(const SSurf* s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t edge) {
    CBox(s, x, y, w, h, CC_CELL, edge);
}

/* 4 x 4 of cells in the content rectangle; returns the cell size. */
static int32_t Compact_GridCell(float rx0, float ry0, float rx1, float ry1, int32_t* gx, int32_t* gy) {
    const int32_t cw = ((int32_t)(rx1 - rx0) - 3 * CC_GAP) / 4, ch = ((int32_t)(ry1 - ry0) - 3 * CC_GAP) / 4;
    const int32_t cell = cw < ch ? cw : ch;
    *gx = (int32_t)rx0 + ((int32_t)(rx1 - rx0) - (cell * 4 + 3 * CC_GAP)) / 2;
    *gy = (int32_t)ry0 + ((int32_t)(ry1 - ry0) - (cell * 4 + 3 * CC_GAP)) / 2;
    return cell;
}

static void Compact_PaintItems(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                               float ry0, float rx1, float ry1, uint32_t tick, int armedRing) {
    FillRect(s, (int32_t)rx0 - 1, (int32_t)ry0 - 2, (int32_t)rx1 + 1, (int32_t)ry1 + 2, CC_BG);
    int32_t gx, gy;
    const int32_t cell = Compact_GridCell(rx0, ry0, rx1, ry1, &gx, &gy);
    int pulseSlot = -1;
    if (armedRing == 1) pulseSlot = snap->equippedSlotA;
    if (armedRing == 2) pulseSlot = snap->equippedSlotB;
    for (int slot = 0; slot < SECOND_SCREEN_ITEM_SLOTS; slot++) {
        const int32_t x = gx + (slot % 4) * (cell + CC_GAP), y = gy + (slot / 4) * (cell + CC_GAP);
        const bool equippedA = slot == snap->equippedSlotA, equippedB = slot == snap->equippedSlotB;
        uint32_t edge = CC_TAB;
        if (equippedA || equippedB) edge = CC_ACCENT;
        if (slot == pulseSlot) edge = (tick / 12u) & 1u ? CC_WARN : CC_ACCENT;
        const uint8_t itemId = snap->menuItems[slot];
        Compact_ItemCell(s, x, y, cell, cell, itemId ? (edge == CC_TAB ? CC_CELL_EDGE : edge) : edge);
        if (itemId == 0) continue;
        uint8_t iconId = itemId;
        if (slot >= 12) {
            const uint8_t content = snap->bottleContents[slot - 12];
            if (content != 0) iconId = content;
        }
        int32_t scale = (cell - 6) / 16;
        if (scale < 1) scale = 1;
        if (scale > 3) scale = 3;
        const int32_t ix = x + (cell - 16 * scale) / 2, iy = y + (cell - 16 * scale) / 2 - 2;
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, ix, iy, scale, iconId);
        DrawItemAmmoCount(s, snap, itemId, x + 2, y + cell - 10, 1);
        if (equippedA || equippedB) {
            CText(s, x + 3, y + 3, CC_ACCENT, equippedA ? "A" : "B");
        }
        AddTarget(tl, (float)x, (float)y, (float)(x + cell), (float)(y + cell), SS_ACT_ITEM, itemId);
    }
}

static void Compact_PaintQuestList(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                                   float ry0, float rx1, float ry1, int view) {
    FillRect(s, (int32_t)rx0 - 1, (int32_t)ry0 - 2, (int32_t)rx1 + 1, (int32_t)ry1 + 2, CC_BG);
    CBox(s, (int32_t)rx0, (int32_t)ry0, (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), CC_CELL, CC_CELL_EDGE);
    const int32_t px = (int32_t)rx0 + 3, py = (int32_t)ry0 + 3, pw = (int32_t)(rx1 - rx0) - 6;
    const int32_t ph = (int32_t)(ry1 - ry0) - 6 - 22;
    if (view == SS_QUEST_KINSTONES)
        Port_SecondScreenQuest_DrawKinstones(s->px, s->w, s->h, s->stride, px, py, pw, ph, snap);
    else
        Port_SecondScreenQuest_DrawTechniques(s->px, s->w, s->h, s->stride, px, py, pw, ph, snap);
    CButton(s, tl, (int32_t)rx0 + 3, (int32_t)ry1 - 21, (int32_t)(rx1 - rx0) - 6, 18, "BACK", CC_TEXT,
            SS_ACT_QUESTVIEW, SS_QUEST_MAIN);
}

static void Compact_PaintQuest(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                               float ry0, float rx1, float ry1, int questView) {
    if (questView == SS_QUEST_KINSTONES || questView == SS_QUEST_TECHNIQUES) {
        Compact_PaintQuestList(s, snap, tl, rx0, ry0, rx1, ry1, questView);
        return;
    }
    FillRect(s, (int32_t)rx0 - 1, (int32_t)ry0 - 2, (int32_t)rx1 + 1, (int32_t)ry1 + 2, CC_BG);
    int32_t gx, gy;
    const int32_t cell = Compact_GridCell(rx0, ry0, rx1, ry1, &gx, &gy);

    /* What each of the sixteen cells holds, as the old grid did. */
    uint8_t icon[16];
    uint16_t count[16];
    uint8_t hasCount[16];
    memset(icon, 0, sizeof(icon));
    memset(count, 0, sizeof(count));
    memset(hasCount, 0, sizeof(hasCount));
    if (snap->tingleTrophy == 1) {
        icon[0] = ITEMID_QST_TINGLE_TROPHY;
    } else if (snap->kinstoneBagOwned) {
        icon[0] = ITEMID_KINSTONE_BAG;
        count[0] = snap->kinstoneFused;
        hasCount[0] = 1;
    }
    if (snap->swordSkills != 0) {
        count[2] = snap->swordSkills;
        hasCount[2] = 1;
    }
    if (snap->carlovMedal == 1) {
        icon[3] = ITEMID_QST_CARLOV_MEDAL;
    } else if (snap->shellsOwned != 0) {
        icon[3] = ITEMID_SHELLS;
        count[3] = snap->shells;
        hasCount[3] = 1;
    }
    for (int i = 0; i < 3; i++) icon[4 + i] = snap->questItems[i];
    if (snap->figurineCount != 0) {
        count[7] = snap->figurineCount;
        hasCount[7] = 1;
    }
    for (int i = 0; i < 4; i++)
        if (snap->elements & (1u << i)) icon[8 + i] = (uint8_t)(ITEMID_EARTH_ELEMENT + i);
    for (int i = 0; i < 3; i++)
        if (snap->passives & (1u << i)) icon[12 + i] = (uint8_t)(ITEMID_GRIP_RING + i);

    for (int slot = 0; slot < 16; slot++) {
        const int32_t x = gx + (slot % 4) * (cell + CC_GAP), y = gy + (slot / 4) * (cell + CC_GAP);
        const bool tappable = (slot == 0 && icon[0] != 0 && icon[0] == ITEMID_KINSTONE_BAG) ||
                              (slot == 2 && hasCount[2]);
        Compact_ItemCell(s, x, y, cell, cell, tappable ? CC_CELL_EDGE : CC_TAB);
        int32_t scale = (cell - 6) / 16;
        if (scale < 1) scale = 1;
        if (scale > 3) scale = 3;
        const int32_t ix = x + (cell - 16 * scale) / 2, iy = y + (cell - 16 * scale) / 2 - 2;
        /* Bag, heart pieces (the game's heart with its n/4) and techniques are
         * the game's own art for those wells. */
        if (slot <= 2 && Port_SecondScreenQuest_DrawCollectionCell(s->px, s->w, s->h, s->stride, slot, x + 2,
                                                                  y + 2, cell - 4, cell - 4, snap)) {
            if (slot == 1) continue;
            if (slot == 2 || (slot == 0 && snap->tingleTrophy != 1)) {
                AddTarget(tl, (float)x, (float)y, (float)(x + cell), (float)(y + cell), SS_ACT_QUESTVIEW,
                          (uint8_t)(slot == 0 ? SS_QUEST_KINSTONES : SS_QUEST_TECHNIQUES));
            }
            continue;
        }
        if (slot == 1) {
            const int hp = snap->heartPieces > 3 ? 3 : snap->heartPieces;
            const SecondScreenThemeSprite* heart = Port_SecondScreenTheme_Get(SST_HEART_EMPTY + hp);
            if (heart) BlitSprite(s, heart, x + (cell - heart->w * 2) / 2, y + (cell - heart->h * 2) / 2 - 2, 2);
            char frac[8];
            snprintf(frac, sizeof(frac), "%d/4", snap->heartPieces);
            CTextCentered(s, x + cell / 2, y + cell - 11, CC_TEXT, frac);
            continue;
        }
        if (slot == 2 && hasCount[2])
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, ix, iy, scale, ITEMID_SMITH_SWORD);
        else if (icon[slot] != 0)
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, ix, iy, scale, icon[slot]);
        if (hasCount[slot]) DrawAmmoCount(s, x + 2, y + cell - 10, 1, count[slot]);
        if (tappable)
            AddTarget(tl, (float)x, (float)y, (float)(x + cell), (float)(y + cell), SS_ACT_QUESTVIEW,
                      (uint8_t)(slot == 0 ? SS_QUEST_KINSTONES : SS_QUEST_TECHNIQUES));
    }
}

/* A dungeon: its name, one button per floor, the floor map beside them. */
static void Compact_PaintDungeonView(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                                     float ry0, float rx1, float ry1, uint32_t tick,
                                     const SecondScreenDungeonMapInfo* info, int viewFloor) {
    {
        /* Name strip, floor buttons down the left, the floor map beside them. */
        FillRect(s, (int32_t)rx0 - 1, (int32_t)ry0 - 2, (int32_t)rx1 + 1, (int32_t)ry1 + 2, CC_BG);
        const char* dname = snap->dungeonIdx < 7 ? kDungeonNames[snap->dungeonIdx] : NULL;
        int32_t top = (int32_t)ry0;
        if (dname) {
            char up[40];
            snprintf(up, sizeof(up), "%s", dname);
            CBox(s, (int32_t)rx0, top, (int32_t)(rx1 - rx0), 16, CC_BAR, CC_TAB_EDGE);
            CTextCentered(s, (int32_t)(rx0 + rx1) / 2, top + 4, CC_TEXT, up);
            top += 19;
        }
        const int8_t topFloor = snap->dungeonIdx < 7 ? kDungeonTopFloor[snap->dungeonIdx] : 2;
        const int32_t bw = 38, bh = 24;
        for (int i = 0; i < info->floorCount && i < 8; i++) {
            const int fl = topFloor - 2 - i;
            char label[8];
            if (fl >= 1) snprintf(label, sizeof(label), "%dF", fl);
            else snprintf(label, sizeof(label), "B%d", 1 - fl);
            const int32_t by = top + i * (bh + 3);
            const bool sel = i == viewFloor;
            CBox(s, (int32_t)rx0, by, bw, bh, sel ? CC_TAB_ON : CC_CELL, sel ? CC_TAB_ON_EDGE : CC_CELL_EDGE);
            CTextCentered(s, (int32_t)rx0 + bw / 2, by + 9, sel ? CC_TAB_ON_TEXT : CC_TEXT, label);
            if (i == info->currentFloor) FillRect(s, (int32_t)rx0 + 3, by + 3, (int32_t)rx0 + 6, by + 6, CC_WARN);
            AddTarget(tl, rx0, (float)by, rx0 + bw, (float)(by + bh), SS_ACT_PLAQUE, (uint8_t)i);
        }
        const int32_t mx0c = (int32_t)rx0 + bw + 4;
        if (!Port_SecondScreenDungeonMap_Draw(s->px, s->w, s->h, s->stride, mx0c, top, (int32_t)rx1 - mx0c,
                                              (int32_t)ry1 - top, snap->dungeonIdx, viewFloor, snap->area,
                                              snap->room, snap->visitedMask, snap->dungeonItemBits,
                                              snap->playerX, snap->playerY, tick)) {
            PaintSchematic(s, snap, mx0c, top, (int32_t)rx1, (int32_t)ry1, tick, 1);
        }
    }
}

static void Compact_PaintNoMap(const SSurf* s, int x0, int y0, int x1, int y1) {
    FillRect(s, x0 - 1, y0 - 2, x1 + 1, y1 + 2, CC_BG);
    CBox(s, x0, y0, x1 - x0, y1 - y0, CC_CELL, CC_CELL_EDGE);
    CTextCentered(s, (x0 + x1) / 2, (y0 + y1) / 2 - 8, CC_TEXT, "NO MAP YET");
    CTextCentered(s, (x0 + x1) / 2, (y0 + y1) / 2 + 4, CC_DIM, "FIND THE HYRULE MAP");
}
