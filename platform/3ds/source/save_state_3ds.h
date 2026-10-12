#pragma once
/* Save states (PLAN P1.8 and P1.8b, docs/save-states.md), in every build.
 *
 * A state is states/tmc-state<id>.bin in the data folder: a header and the
 * game's globals by name, with their pointers written as relocations, so a
 * state loads into any build of the same ROM (format 2); a binary without the
 * symbol table (tools/gen_state_symbols.py) writes the raw format 1, which
 * loads only where the game's layout is the same and is refused, never half
 * applied, elsewhere. states/tmc-state<id>.thm holds a 200x120 RGB565
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
/* The job waiting or running (NONE when idle), for a "saving..." notice. The job starts a few
 * frames after it is asked, so that a notice can be on the screen before the game stops. */
SaveStateJob SaveState_PendingJob(void);
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
