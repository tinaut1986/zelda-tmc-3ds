/*
 * 3DS-only wrapper around the shared Minish Cap second-screen compositor.
 *
 * Keeping the wrapper in platform/3ds makes the title/file-select behavior
 * independent of Android and desktop. The shared compositor is included as
 * this translation unit's implementation. Outside gameplay (intro, title,
 * file select) the whole bottom screen still shows: every tab, OPTIONS and
 * DEBUG working, the game tabs saying they need a game (sCompactNoGame).
 */
#include "port_second_screen_3ds.h"
#include "bottom_frame_state_3ds.h"
#include "bottom_map_anim_3ds.h"

#include <stdbool.h>

/* This replaces the direct port/port_second_screen.c entry in the 3DS CMake
 * source list. All normal exported functions retain their original symbols. */
#include "../../../port/port_second_screen.c"

static uint8_t sIdleOwnsSettingsTab;
static BottomFrameState3DS sFrameState;

static uint32_t BeginRefresh(void) {
    return BottomFrameState3DS_BeginPaint(&sFrameState);
}

static void FinishRefresh(uint32_t request) {
    /* A tap may arrive while the worker is painting.  Acknowledge only the
     * generation this frame actually observed; a newer generation remains
     * pending and schedules another paint instead of being lost. */
    BottomFrameState3DS_FinishPaint(&sFrameState, request);
}

static uint32_t RequestRefresh(void) {
    return BottomFrameState3DS_Request(&sFrameState);
}

static uint32_t RequestTouchRefresh(void) {
    return BottomFrameState3DS_RequestTouchRefresh(&sFrameState);
}

static void ResetIdleOnlyState(void) {
    UI_LOCK();
    sTapTargetCount = 0;
    sUi.mapLive = 0;
    sMapCameraMoving = 0;
    sUi.armedRing = 0;
    sUi.floorPreview = SS_NO_FLOOR;
    sUi.playerFloorDisp = SS_NO_FLOOR;
    sUi.regionState = SS_REGION_OFF;
    sUi.questView = SS_QUEST_MAIN;
    sUi.settingsPage = SS_SETTINGS_ROOT;
    sUi.loadConfirmActive = 0;
    sUi.unavailableNoticeActive = 0;
    if (sIdleOwnsSettingsTab) sUi.tab = SS_TAB_MAP;
    UI_UNLOCK();
    sLastFix.valid = 0;
    sCam.valid = 0;
    sIdleOwnsSettingsTab = 0;
}


uint32_t Port_SecondScreen_3DS_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                        const SecondScreenSnapshot* snap, uint32_t tick) {
    if (!pixels || !snap || width <= 0 || height <= 0 || strideInPixels < width) return 0;

    const uint32_t refreshRequest = BeginRefresh();

    if (snap->inGame) {
        if (sIdleOwnsSettingsTab) ResetIdleOnlyState();
        Port_SecondScreen_PaintInto(pixels, width, height, strideInPixels, snap, tick);
        FinishRefresh(refreshRequest);
        return refreshRequest;
    }

    /* No game running: the same UI, opening on OPTIONS. Its values come from
     * the port configuration, not from gameplay, so a zeroed snapshot is safe;
     * the game tabs paint a notice instead of reading it (sCompactNoGame).
     * Map fixes, armed items and submenus must not cross a title/file-select
     * boundary, hence the reset on the way in. */
    if (!sIdleOwnsSettingsTab) {
        ResetIdleOnlyState();
        UI_LOCK();
        sUi.tab = SS_TAB_SETTINGS;
        UI_UNLOCK();
        sIdleOwnsSettingsTab = 1;
    }
    SecondScreenSnapshot menuSnapshot = *snap;
    menuSnapshot.inGame = 1;
    sCompactNoGame = 1;
    Port_SecondScreen_PaintInto(pixels, width, height, strideInPixels, &menuSnapshot, tick);
    sCompactNoGame = 0;
    FinishRefresh(refreshRequest);
    return refreshRequest;
}

void Port_SecondScreen_3DS_ResetFrameState(void) {
    BottomFrameState3DS_Reset(&sFrameState);
}

uint32_t Port_SecondScreen_3DS_RequestRefresh(void) {
    return RequestRefresh();
}

int Port_SecondScreen_3DS_IsShown(uint32_t generation) {
    BottomFrameState3DSStats stats;
    BottomFrameState3DS_GetStats(&sFrameState, &stats);
    return stats.visible != 0 && (int32_t)(stats.visible - generation) >= 0;
}

void Port_SecondScreen_3DS_MarkSubmitted(uint32_t generation, int inGame) {
    BottomFrameState3DS_MarkSubmitted(&sFrameState, generation, inGame);
}

void Port_SecondScreen_3DS_PromoteSubmitted(void) {
    BottomFrameState3DS_PromoteSubmitted(&sFrameState);
}

void Port_SecondScreen_3DS_GetFrameStats(BottomFrameState3DSStats* out) {
    BottomFrameState3DS_GetStats(&sFrameState, out);
}

extern int Port_SecondScreen_TapIsTab(int x, int y); /* port_second_screen.c */

/* A tap that came while the panel it was aimed at was being repainted, replayed
 * once the new panel is shown (Port_SecondScreen_3DS_ServicePendingTap, each
 * tick after the VBlank); forgotten after PENDING_TAP_TICKS logic ticks. Main
 * thread only, like OnTap. */
enum { PENDING_TAP_TICKS = 45 };
static struct {
    int x, y, longPress;
    unsigned ticksLeft;
} sPendingTap;

void Port_SecondScreen_3DS_OnTap(int x, int y, int longPress) {
    SecondScreenSnapshot currentSnapshot;
    Port_SecondScreenState_Read(&currentSnapshot);
    /* Painting publishes the compositor's new hit boxes before its texture
     * can be submitted or shown.  Dispatch only when those hit boxes belong
     * to the confirmed visible generation and task; otherwise fail closed
     * while leaving the per-tick HID polling cadence untouched. */
    if (!BottomFrameState3DS_CanDispatchTouch(&sFrameState, currentSnapshot.inGame != 0)) {
        /* The tab bar is the same in every panel: a tap on it is safe against hit boxes of a
         * panel that is not yet the visible one. Dropping it made a tab (above all the one
         * tapped just after another, while the first one's repaint was in flight) need
         * several taps. */
        if (BottomFrameState3DS_VisibleModeMatches(&sFrameState, currentSnapshot.inGame != 0) &&
            Port_SecondScreen_TapIsTab(x, y)) {
            Port_SecondScreen_OnTap(x, y, longPress);
            RequestTouchRefresh();
            return;
        }
        /* A same-task hidden paint is already on its way to the display; an
         * extra request would only lengthen this fail-closed interval.  Task
         * transitions (and the pre-first-frame state) still need a paint. */
        if (!BottomFrameState3DS_VisibleModeMatches(&sFrameState, currentSnapshot.inGame != 0)) {
            RequestTouchRefresh();
            return;
        }
        /* Kept, not dropped: the player tapped what is on screen, and the panel
         * that answers the previous tap is only a paint away. Dropping it made
         * quick taps need several tries. */
        sPendingTap.x = x;
        sPendingTap.y = y;
        sPendingTap.longPress = longPress;
        sPendingTap.ticksLeft = PENDING_TAP_TICKS;
        return;
    }
    sPendingTap.ticksLeft = 0;
    Port_SecondScreen_OnTap(x, y, longPress);
    RequestTouchRefresh();
}

void Port_SecondScreen_3DS_ServicePendingTap(void) {
    if (sPendingTap.ticksLeft == 0) return;
    --sPendingTap.ticksLeft;
    SecondScreenSnapshot currentSnapshot;
    Port_SecondScreenState_Read(&currentSnapshot);
    if (!BottomFrameState3DS_VisibleModeMatches(&sFrameState, currentSnapshot.inGame != 0)) {
        sPendingTap.ticksLeft = 0;
        return;
    }
    if (!BottomFrameState3DS_CanDispatchTouch(&sFrameState, currentSnapshot.inGame != 0)) return;
    sPendingTap.ticksLeft = 0;
    Port_SecondScreen_OnTap(sPendingTap.x, sPendingTap.y, sPendingTap.longPress);
    RequestTouchRefresh();
}

int Port_SecondScreen_3DS_NeedsRefresh(void) {
    return BottomFrameState3DS_NeedsPaint(&sFrameState);
}

extern int Port_SecondScreen_MapPhaseChanged(const SecondScreenSnapshot* snap);
int Port_SecondScreen_3DS_NeedsPeriodicRefresh(const SecondScreenSnapshot* snap, uint32_t tick,
                                               uint32_t paintedTick, int32_t width,
                                               int32_t height) {
    if (!snap) return 0;

    /* The boot update prompt (AUTO UPDATE) shows and counts its progress over any tab: paint while it
     * is up, and once more to take it away. */
    {
        static int sAutoPromptShown;
        const int up = Updater_AutoPrompt() != UPDATE_PROMPT_NONE;
        if (up || sAutoPromptShown) {
            sAutoPromptShown = up;
            return 1;
        }
    }

    /* The clock, Wi-Fi bars and battery in the tab bar. */
    if (Compact_ChromeChanged() || Compact_ToastNeedsPaint() || Compact_RaToastNeedsPaint() || Compact_BusyNeedsPaint() || Compact_DevFtpNeedsPaint())
        return 1;

    int tab;
    int settingsPage;
    uint32_t dumpFlashUntil;
    uint32_t lastTick;
    int regionState;
    int floorPreview;
    int cameraMoving;
    UI_LOCK();
    tab = sUi.tab;
    settingsPage = sUi.settingsPage;
    dumpFlashUntil = sUi.dumpFlashUntil;
    lastTick = sUi.lastTick;
    regionState = sUi.regionState;
    floorPreview = sUi.floorPreview;
    cameraMoving = sMapCameraMoving;
    UI_UNLOCK();

    /* Without a game the MAP, ITEMS and QUEST tabs are a still notice. */
    if (tab == SS_TAB_STATES) return Compact_StatesNeedsPaint();
    if (tab == SS_TAB_RETRO) return Compact_RetroAchNeedsPaint();
    if (!snap->inGame && tab != SS_TAB_SETTINGS && tab != SS_TAB_DEBUG) return 0;

    if (tab == SS_TAB_MAP) {
        if (!Port_Config_BottomMapSkip()) {
            return 1; /* the old unconditional repaint, kept for the A/B */
        }
        /* The camera glide, region bracket and floor preview advance inside
         * paint. Keep repainting until they settle, even when the engine
         * snapshot and marker animation signature have not changed. */
        if (cameraMoving || regionState == SS_REGION_BRACKET || floorPreview != SS_NO_FLOOR) {
            return 1;
        }
        if (Port_SecondScreen_MapPhaseChanged(snap)) return 1; /* the hints and Link swap every 32 game frames */
        if (BottomMapAnim_NeedsPaint(tick, paintedTick,
                                     (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0, width,
                                     height)) {
            return 1;
        }
        return 0;
    }
    if (tab == SS_TAB_ITEMS) {
        /* The authentic item cursor blinks even when no values change. */
        return 1;
    }
    if (tab == SS_TAB_DEBUG) {
        /* Live timings and position. */
        return 1;
    }
    if (tab != SS_TAB_SETTINGS) {
        /* Quest status and its two detail lists have no selection cursor. */
        return 0;
    }
    if (settingsPage == SS_SETTINGS_UPDATE) return 1;
    if (settingsPage == SS_SETTINGS_OVERLAY) {
        /* Live diagnostics deliberately retain their slower refresh rate. */
        return 1;
    }
    if (settingsPage == SS_SETTINGS_DEVELOPER &&
        (int32_t)(dumpFlashUntil - lastTick) > 0) {
        /* Keep painting until the post-dump DONE indicator expires. */
        return 1;
    }
    return 0;
}

/* What the sidebar's HUD shows (the R prompt, hearts, rupees, keys, the charge
 * meter, the equipped items): a change of these is painted at once, not at the
 * next cadence tick, since it answers what the player just did. */
int Port_SecondScreen_3DS_HudChanged(const SecondScreenSnapshot* previous, const SecondScreenSnapshot* current) {
    if (!previous || !current || !current->inGame) return 0;
    return previous->rActionFrame != current->rActionFrame || previous->health != current->health ||
           previous->maxHealth != current->maxHealth || previous->rupees != current->rupees ||
           previous->dungeonKeys != current->dungeonKeys || previous->topHudHidden != current->topHudHidden ||
           previous->chargeAction != current->chargeAction ||
           (current->chargeAction != 0 && previous->chargeTimer != current->chargeTimer) ||
           previous->equippedA != current->equippedA || previous->equippedB != current->equippedB;
}

int Port_SecondScreen_3DS_SnapshotChangeNeedsRefresh(const SecondScreenSnapshot* previous,
                                                     const SecondScreenSnapshot* current,
                                                     int previousValid) {
    if (!previous || !current || !previousValid) return 1;
    if (previous->inGame != current->inGame || previous->introCinema != current->introCinema) return 1;
    if (memcmp(previous, current, sizeof(*current)) == 0) return 0;

    /* Gameplay snapshots continue changing while a settings sheet covers the
     * entire panel, but none of those engine fields are drawn there.  Config
     * mutations originate in the tap handler and request an immediate paint;
     * the overlay and dump indicator are handled by NeedsPeriodicRefresh. */
    if (!current->inGame) return 0;
    int tab;
    UI_LOCK();
    tab = sUi.tab;
    UI_UNLOCK();
    return tab != SS_TAB_SETTINGS;
}
