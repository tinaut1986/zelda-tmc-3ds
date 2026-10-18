/* The UPDATE page and its changelog window, in the compact style of
 * bottom_compact_3ds.inc (cells, 5x7 font). It all lives on the bottom screen;
 * the top one keeps showing the game. Included by port/port_second_screen.c after
 * bottom_compact_3ds.inc, whose boxes, colours and text it uses. */
#include "updater.h"
#include <ctype.h>

static bool sUpdateConfirm, sUpdateNotes;
static bool sAutoNotes; /* the changelog window opened from the boot prompt (WHAT'S NEW) */
static unsigned sUpdatePage, sUpdatePages = 1;

static void UpdateUI_Reset(void) {
    UI_LOCK();
    sUpdateConfirm = sUpdateNotes = false;
    sUpdatePage = 0;
    UI_UNLOCK();
}

bool Port_SecondScreen_3DS_UpdateOpen(void) {
    UI_LOCK();
    bool open = sUi.tab == SS_TAB_SETTINGS && sUi.settingsPage == SS_SETTINGS_UPDATE;
    UI_UNLOCK();
    return open;
}

static void UpdateUpper(char* out, size_t cap, const char* text) {
    snprintf(out, cap, "%s", text);
    for (char* c = out; *c; ++c) *c = (char)toupper((unsigned char)*c);
}

/* Words of `text` broken into lines of at most `cols` characters. */
static unsigned UpdateWrap(const char* text, unsigned cols, char (*lines)[64], unsigned maxLines) {
    unsigned n = 0;
    while (*text && n < maxLines) {
        while (*text == ' ') ++text;
        size_t len = strlen(text);
        size_t take = len < cols ? len : cols;
        if (take < len) {
            size_t cut = take;
            while (cut > 0 && text[cut] != ' ') --cut;
            if (cut > 0) take = cut;
        }
        if (take == 0) break;
        memcpy(lines[n], text, take);
        lines[n][take] = 0;
        ++n;
        text += take;
    }
    return n;
}

enum { UPDATE_NOTE_COLS = 51, UPDATE_NOTE_MAX = 512 };
static char sNoteLines[UPDATE_NOTE_MAX][UPDATE_NOTE_COLS + 1];
static unsigned sNoteCount, sNoteRevision = UINT32_MAX;
static bool sNoteLoaded;

/* The release's notes as wrapped lines (a bullet's continuation indented),
 * rebuilt when the updater's data changes. */
static void UpdateLoadNotes(const UpdateStatus* st) {
    if (sNoteLoaded && sNoteRevision == st->revision) return;
    static char body[12289], formatted[UPDATE_NOTE_MAX][43];
    Updater_GetNotes(body, sizeof(body));
    const unsigned n = Update_FormatNotes(body, formatted, UPDATE_NOTE_MAX);
    sNoteCount = 0;
    char para[512];
    para[0] = 0;
    for (unsigned i = 0; i <= n && sNoteCount < UPDATE_NOTE_MAX; ++i) {
        const char* line = i < n ? formatted[i] : "";
        while (*line == ' ') ++line;
        const bool bullet = line[0] == '-' && line[1] == ' ';
        if (para[0] && (!line[0] || bullet)) {
            UpdateUpper(para, sizeof(para), para);
            const bool listed = para[0] == '-';
            char wrapped[24][64];
            const unsigned m = UpdateWrap(para, UPDATE_NOTE_COLS - (listed ? 2u : 0u), wrapped, 24);
            for (unsigned k = 0; k < m && sNoteCount < UPDATE_NOTE_MAX; ++k)
                snprintf(sNoteLines[sNoteCount++], sizeof(sNoteLines[0]), "%s%s", listed && k ? "  " : "", wrapped[k]);
            para[0] = 0;
        }
        if (!line[0]) {
            if (i < n && sNoteCount < UPDATE_NOTE_MAX) sNoteLines[sNoteCount++][0] = 0;
            continue;
        }
        if (para[0]) strncat(para, " ", sizeof(para) - strlen(para) - 1);
        strncat(para, line, sizeof(para) - strlen(para) - 1);
    }
    sNoteRevision = st->revision;
    sNoteLoaded = true;
}

/* The changelog window: the lines, paged with PREV / NEXT, CLOSE back to the page. */
static void PaintUpdateNotes(const SSurf* s, TargetList* tl, const UpdateStatus* st, bool fromPrompt) {
    const int32_t top = CcContentTop(), bottom = CcContentBottom(s->h), w = s->w - 2 * CC_MARGIN;
    FillRect(s, 0, top, s->w, bottom, CC_BG);
    UpdateLoadNotes(st);
    const int32_t listY = top + 2 + 16 + CC_GAP, buttonsY = bottom - 24 - 2;
    const unsigned perPage = (unsigned)(buttonsY - CC_GAP - listY - 6) / 10u;
    unsigned pages = (sNoteCount + perPage - 1) / perPage;
    if (!pages) pages = 1;
    unsigned page;
    UI_LOCK();
    if (sUpdatePage >= pages) sUpdatePage = pages - 1;
    page = sUpdatePage;
    sUpdatePages = pages;
    UI_UNLOCK();

    char head[80], upper[48];
    UpdateUpper(upper, sizeof(upper), st->version);
    snprintf(head, sizeof(head), "%s %s", Port_Text_Tr("CHANGELOG"), upper);
    if (fromPrompt) {
        char count[24];
        snprintf(count, sizeof(count), "  %u/%u", page + 1, pages);
        strncat(head, count, sizeof(head) - strlen(head) - 1);
    }
    CBox(s, CC_MARGIN, top + 2, w, 16, CC_BAR, CC_TAB_EDGE);
    CTextCentered(s, s->w / 2, top + 6, CC_TEXT, head);
    CBox(s, CC_MARGIN, listY, w, buttonsY - CC_GAP - listY, CC_CELL, CC_CELL_EDGE);
    for (unsigned i = page * perPage; i < sNoteCount && i < (page + 1) * perPage; ++i)
        CText(s, CC_MARGIN + 6, listY + 4 + (int32_t)(i - page * perPage) * 10, CC_TEXT, sNoteLines[i]);
    CButton(s, tl, CC_MARGIN, buttonsY, 64, 24, "PREV", page ? CC_TEXT : CC_FAINT, SS_ACT_UPDATE_PREV, 0);
    CButton(s, tl, CC_MARGIN + w - 64, buttonsY, 64, 24, "NEXT", page + 1 < pages ? CC_TEXT : CC_FAINT,
            SS_ACT_UPDATE_NEXT, 0);
    char foot[32];
    snprintf(foot, sizeof(foot), "%s %u %s %u", Port_Text_Tr("PAGE"), page + 1, Port_Text_Tr("OF"), pages);
    if (fromPrompt) /* back to the question */
        CButton(s, tl, CC_MARGIN + 64 + CC_GAP, buttonsY, w - 128 - 2 * CC_GAP, 24, "CLOSE", CC_TEXT, SS_ACT_AUTOUPD_NEWS, 0);
    else
        CButton(s, tl, CC_MARGIN + 64 + CC_GAP, buttonsY, w - 128 - 2 * CC_GAP, 24, foot, CC_DIM, SS_ACT_UPDATE_RELEASE, 0);
}

static void PaintUpdatePanel(const SSurf* s, TargetList* tl) {
    UpdateStatus st;
    Updater_GetStatus(&st);
    bool confirm, notes;
    unsigned page, pages;
    UI_LOCK();
    confirm = sUpdateConfirm;
    notes = sUpdateNotes;
    page = sUpdatePage;
    pages = sUpdatePages;
    UI_UNLOCK();
    if (notes && !confirm) {
        PaintUpdateNotes(s, tl, &st, false);
        return;
    }
    const bool busy = Updater_Busy();
    const int32_t top = CcContentTop();
    const int32_t bottom = CcContentBottom(s->h);
    const int32_t w = s->w - 2 * CC_MARGIN;
    FillRect(s, 0, top, s->w, bottom, CC_BG);

    int32_t y = top + 2;
    int32_t x, cw;
    char text[96];
    CColumn(s->w, 3, 0, 1, &x, &cw);
    CCell(s, tl, x, y, cw, CC_CELL_H, "CHANNEL", st.prerelease ? "+ BETAS" : "STABLE", CC_TEXT, SS_ACT_UPDATE_CHANNEL, 0);
    CColumn(s->w, 3, 1, 1, &x, &cw);
    CCell(s, tl, x, y, cw, CC_CELL_H, "AUTO UPDATE", Port_Config_GetAutoUpdate() ? "ON" : "OFF",
          Port_Config_GetAutoUpdate() ? CC_GOOD : CC_DIM, SS_ACT_UPDATE_AUTO, 0);
    CColumn(s->w, 3, 2, 1, &x, &cw);
    /* st.version already says BETA when the release found is a pre-release. */
    if (st.version[0]) {
        UpdateUpper(text, sizeof(text), st.version);
    } else {
        UpdateUpper(text, sizeof(text), Port_Text_Tr("NONE"));
    }
    CCell(s, tl, x, y, cw, CC_CELL_H, "RELEASE", text, notes ? CC_ACCENT : CC_TEXT, SS_ACT_UPDATE_RELEASE, 0);
    y += CC_CELL_H + CC_GAP;

    /* Status: what the updater says, or the question while a install waits. */
    const char* message = confirm ? "DOWNLOAD AND INSTALL THIS VERSION?" : st.message;
    if (!message[0]) message = "PRESS CHECK TO LOOK FOR A NEW VERSION";
    UpdateUpper(text, sizeof(text), Port_Text_Tr(message));
    char lines[4][64];
    unsigned n = UpdateWrap(text, (unsigned)(w - 10) / kUiAdvance, lines, 3);
    uint32_t color = st.state == UPDATE_ERROR ? CC_BAD : st.state == UPDATE_AVAILABLE || confirm ? CC_WARN
                     : st.state == UPDATE_DONE || st.state == UPDATE_CURRENT ? CC_GOOD : CC_TEXT;
    const int32_t statusH = 44;
    CBox(s, CC_MARGIN, y, w, statusH, CC_CELL, CC_TAB);
    CText(s, CC_MARGIN + 4, y + 4, CC_DIM, "STATUS");
    /* What is installed, with its channel: "0.1.2 BETA", "0.1.2-DEV.54+758FD59". */
    {
        char installed[64];
        snprintf(installed, sizeof(installed), "%s V", Port_Text_Tr("INSTALLED"));
        UpdateUpper(text, sizeof(text), TMC_PORT_VERSION_LABEL);
        strncat(installed, text, sizeof(installed) - strlen(installed) - 1);
        CText(s, CC_MARGIN + w - 4 - CTextWidth(installed), y + 4, CC_DIM, installed);
    }
    for (unsigned i = 0; i < n; ++i) CText(s, CC_MARGIN + 4, y + 14 + (int32_t)i * 10, color, lines[i]);
    y += statusH + CC_GAP;

    const bool working = st.state == UPDATE_DOWNLOADING || st.state == UPDATE_VERIFYING || st.state == UPDATE_INSTALLING;
    if (working) {
        FillRect(s, CC_MARGIN, y, CC_MARGIN + w, y + 8, CC_CELL_EDGE);
        FillRect(s, CC_MARGIN + 1, y + 1, CC_MARGIN + w - 1, y + 7, CC_BAR);
        int32_t fill = (w - 2) * (int32_t)(st.progress > 100 ? 100 : st.progress) / 100;
        FillRect(s, CC_MARGIN + 1, y + 1, CC_MARGIN + 1 + fill, y + 7, CC_GOOD);
        y += 8 + CC_GAP;
    }

    CTextCentered(s, s->w / 2, y + 6, CC_FAINT,
                  confirm ? "UNSAVED PROGRESS IS LOST" : "TAP RELEASE FOR THE CHANGELOG");

    /* Back and the one action. */
    const int32_t by = bottom - 26;
    CButton(s, tl, CC_MARGIN, by, 70, 24, "BACK", CC_TEXT, SS_ACT_SETTINGS_BACK, SS_SETTINGS_ROOT);
    const char* action = confirm ? "INSTALL UPDATE" : st.state == UPDATE_INSTALLING ? "PLEASE WAIT"
                         : busy ? "CANCEL" : st.state == UPDATE_AVAILABLE ? "DOWNLOAD UPDATE"
                         : st.state == UPDATE_DONE ? "UPDATE INSTALLED" : "CHECK FOR UPDATE";
    CButton(s, tl, CC_MARGIN + 70 + CC_GAP, by, w - 70 - CC_GAP, 24, action,
            confirm ? CC_WARN : CC_TEXT, SS_ACT_UPDATE_ACTION, 0);
}

/* Taps of the changelog window opened from the boot prompt; true when it swallowed the tap. */
static bool HandleAutoNotesTap(int action) {
    if (!sAutoNotes) return false;
    UI_LOCK();
    if (action == SS_ACT_AUTOUPD_NEWS) { sAutoNotes = false; }
    else if (action == SS_ACT_UPDATE_PREV) { if (sUpdatePage) --sUpdatePage; }
    else if (action == SS_ACT_UPDATE_NEXT) { if (sUpdatePage + 1 < sUpdatePages) ++sUpdatePage; }
    UI_UNLOCK();
    return true;
}

static bool HandleUpdateTap(int action, int arg) {
    if (!Port_SecondScreen_3DS_UpdateOpen()) return false;
    UpdateStatus st; Updater_GetStatus(&st);
    if (st.state == UPDATE_INSTALLING || st.state == UPDATE_DONE) return true;
    bool busy = Updater_Busy();
    if (action == SS_ACT_SETTINGS_BACK) {
        UI_LOCK();
        bool confirm = sUpdateConfirm, notes = sUpdateNotes;
        sUpdateConfirm = false;
        sUpdateNotes = false;
        UI_UNLOCK();
        if (confirm || notes) return true;
        Updater_Cancel();
        if (busy) return true; // Wait for the worker before resuming the game.
        UpdateUI_Reset();
        return false;
    }
    if (action == SS_ACT_TAB) {
        if (busy) return true;
        UpdateUI_Reset(); return false;
    }
    if (action == SS_ACT_UPDATE_ACTION) {
        if (busy) { Updater_Cancel(); return true; }
        UI_LOCK(); bool confirm = sUpdateConfirm; sUpdateConfirm = false; UI_UNLOCK();
        if (confirm && st.state == UPDATE_AVAILABLE) Updater_Download();
        else if (st.state == UPDATE_AVAILABLE) {
            UI_LOCK(); sUpdateConfirm = true; UI_UNLOCK();
        } else Updater_Check();
        return true;
    }
    if (busy) return true;
    switch (action) {
        case SS_ACT_UPDATE_CHANNEL:
            UpdateUI_Reset(); Updater_SetChannel(!st.prerelease); break;
        case SS_ACT_UPDATE_AUTO:
            Port_Config_ToggleAutoUpdate();
            break;
        case SS_ACT_UPDATE_RELEASE:
            UI_LOCK();
            if (sUpdateNotes) sUpdateNotes = false;
            else if (st.version[0]) { sUpdateNotes = true; sUpdatePage = 0; }
            UI_UNLOCK();
            break;
        case SS_ACT_UPDATE_PREV:
            UI_LOCK(); if (sUpdatePage) --sUpdatePage; UI_UNLOCK(); break;
        case SS_ACT_UPDATE_NEXT:
            UI_LOCK(); if (sUpdatePage+1 < sUpdatePages) ++sUpdatePage; UI_UNLOCK(); break;
        default: return true;
    }
    (void)arg;
    return true;
}

