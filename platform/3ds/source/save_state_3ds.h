#pragma once
/* Save states (PLAN P1.8, docs/save-states.md), in every build.
 *
 * A state is states/tmc-state<id>.bin in the data folder: a header and a raw
 * copy of the game's .data and .bss, which 3dsx.ld lays out at fixed
 * addresses together with the game's code and constants. A state therefore
 * loads into any build whose game side is unchanged (renderer, UI and
 * platform changes do not matter) and is refused, never half applied, by a
 * build that moved the game. states/tmc-state<id>.thm holds a 200x120 RGB565
 * screenshot of the top screen.
 *
 * The list is open ended, newest first; the number shown is the position in
 * it, the id only names the file. Everything that touches the SD card or the
 * game is a job: any thread asks (the bottom screen, the PC link), the game's
 * main loop runs it between frames (SaveState_Service in AgbMain). */
#include <stdbool.h>
#include <stdint.h>

enum { SAVE_STATE_THUMB_W = 200, SAVE_STATE_THUMB_H = 120 };

typedef enum SaveStateJob {
    SAVE_STATE_JOB_NONE,
    SAVE_STATE_JOB_SCAN,
    SAVE_STATE_JOB_SAVE_NEW,
    SAVE_STATE_JOB_OVERWRITE,
    SAVE_STATE_JOB_LOAD,
    SAVE_STATE_JOB_DELETE,
} SaveStateJob;

typedef struct SaveStateInfo {
    uint32_t id;
    uint32_t savedAt; /* Unix seconds, console clock */
    uint8_t area, room;
    uint8_t health, maxHealth; /* 8 per heart */
    uint16_t rupees;
    bool compatible; /* this build can load it */
    bool hasThumb;
    char build[24];  /* the version that saved it */
} SaveStateInfo;

/* Any thread. False when another job is still waiting or running. `id` is
 * ignored by SCAN and SAVE_NEW. */
bool SaveState_Request(SaveStateJob job, uint32_t id);
bool SaveState_Busy(void);
/* Rises by one each time a job ends; with SaveState_LastResult a caller can
 * wait for the job it asked for. */
uint32_t SaveState_Generation(void);
/* The last job's outcome: ok, its message ("STATE 2 SAVED") and the id it
 * concerned (the new id for SAVE_NEW). */
bool SaveState_LastResult(char* message, unsigned cap, uint32_t* id);
/* True while a state can be saved: a game is running. Loading works anywhere. */
bool SaveState_CanSave(void);

/* The list, newest first (a consistent copy; any thread). */
int SaveState_Count(void);
bool SaveState_InfoAt(int index, SaveStateInfo* out);
/* Reads a state's screenshot (SAVE_STATE_THUMB_W x H RGB565). Any thread. */
bool SaveState_ReadThumb(uint32_t id, uint16_t* pixels);
/* "HYRULE FIELD", or NULL for an unknown area. */
const char* SaveState_AreaName(unsigned area);

/* The game's main loop, between frames: runs the pending job. */
void SaveState_Service(void);
