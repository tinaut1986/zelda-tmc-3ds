/* RetroAchievements for The Minish Cap 3DS, on top of rcheevos (third_party/rcheevos),
 * adapted from ../sm-3ds/source/retro_ach.h (docs/retroachievements.md).
 *
 * The game's globals are read at their original GBA addresses through a generated table
 * (retro_ach_map_3ds.c, tools/gen_ra_map.py), so the existing Minish Cap sets run on it.
 * Softcore only: RetroAchievements does not sanction unofficial ports. Softcore allows save
 * states, so none of them stops the set.
 *
 * Every rc_client call is made from the main thread; HTTP, the ROM hash and the badges run
 * on a worker thread and their results are handed back in RetroAch_Update. The bottom
 * screen paints on its own thread: what it reads goes through the copying accessors below.
 * Login: the system keyboard asks for the user name and password once; the token the
 * server returns is kept in retroachievements.ini in the data folder. Log:
 * debug/retroachievements.log. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    kRaOff,         /* switched off */
    kRaNoAccount,   /* on, but nothing to log in with */
    kRaConnecting,
    kRaOnline,
    kRaOffline,     /* the server could not be reached (unlocks wait and are retried) */
    kRaLoginError,  /* the server rejected the login */
} RaStatus;

/* RA's achievement types (rcheevos' RC_CLIENT_ACHIEVEMENT_TYPE_*, same values). */
typedef enum { kRaTypeStandard, kRaTypeMissable, kRaTypeProgression, kRaTypeWin } RaType;

typedef struct {
    uint32_t id;
    char title[64];
    char description[128];
    char badge[16];       /* RA's badge name, "" for none (RetroAch_Badge) */
    uint32_t points;
    RaType type;
    bool unlocked;
    uint32_t unlock_time; /* seconds since 1970, 0 when locked */
} RaAchievement;

/* Badge sizes RetroAch_Badge has: the list's cards and the detail view. */
enum { kRaBadgeSmall = 28, kRaBadgeBig = 64 };

/* How the list is ordered (mzm's choices): as rcheevos groups it (locked first), by
 * title, by points, or by unlock time (still locked ones last). */
typedef enum { kRaSortDefault, kRaSortTitle, kRaSortPoints, kRaSortRecent, kRaSortCount } RaSort;

/* Init reads retroachievements.ini and logs in if it is on and has a token. After the
 * ROM is loaded and the data folder is the working directory. */
void RetroAch_Init(void);
void RetroAch_Shutdown(void);
/* Every main loop iteration (network responses, keep-alive, the notice's timer). */
void RetroAch_Update(void);
/* After each game frame. */
void RetroAch_DoFrame(void);
/* The game was reset; a save state was saved, loaded or deleted. The progress of each
 * achievement goes with the state, in states/tmc-state<id>.rap. */
void RetroAch_GameReset(void);
void RetroAch_StateSaved(uint32_t id);
void RetroAch_StateLoaded(uint32_t id);
void RetroAch_StateDeleted(uint32_t id);

bool RetroAch_Enabled(void);
void RetroAch_SetEnabled(bool on);
/* Asks for the user name and password with the system keyboard (blocks while it shows;
 * main thread). */
void RetroAch_PromptLogin(void);
void RetroAch_Logout(void);
/* Logged in, or logging in: the account cell offers LOG OUT. */
bool RetroAch_LoggedIn(void);

RaStatus RetroAch_Status(void);
/* Copies, safe from any thread. */
void RetroAch_User(char* out, int cap);
/* Last thing worth telling the player (server error, "ROM not recognised"...), or "". */
void RetroAch_Message(char* out, int cap);
bool RetroAch_GameLoaded(void);

int RetroAch_Count(void);
/* The i-th achievement in the list's order (RetroAch_SetSort); false past the end. */
bool RetroAch_CopyAt(int i, RaAchievement* out);
int RetroAch_UnlockedCount(void);
uint32_t RetroAch_Points(bool unlocked_only);

/* An achievement's badge, `size` x `size` (kRaBadgeSmall or kRaBadgeBig) in row-major
 * order, each pixel in the bottom screen's RGB() layout (also C2D_Color32's); NULL until
 * it has been loaded. Badges are downloaded on the worker thread the first time and kept
 * in badges/ in the data folder; asking for one moves it to the front of the queue. The
 * pixels stay valid until RetroAch_Shutdown. */
const uint32_t* RetroAch_Badge(const char* badge, int size);

/* Bumped whenever anything above changes (a badge arriving too), so the UI knows to redraw. */
uint32_t RetroAch_Version(void);

/* The last unlock while its notice should show (3 s); false otherwise. */
bool RetroAch_Toast(RaAchievement* out);

/* Settings, kept in retroachievements.ini like mzm's: the notice on the top screen
 * (otherwise the bottom one), the unlock sound, the list's order. */
bool RetroAch_NotifyTop(void);
void RetroAch_SetNotifyTop(bool top);
bool RetroAch_Sound(void);
void RetroAch_SetSound(bool on);
RaSort RetroAch_Sort(void);
bool RetroAch_Descending(void);
void RetroAch_SetSort(RaSort sort, bool descending);
/* A sample notice (and the sound), to see where it shows. */
void RetroAch_ShowPreview(void);
/* Adds the unlock sound, when one is playing, to `frames` frames of 16-bit stereo audio
 * at `rate` Hz. Called from the audio thread. */
void RetroAch_MixAudio(int16_t* out, int frames, int rate);
