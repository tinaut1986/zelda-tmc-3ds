/* Save states: see save_state_3ds.h and docs/save-states.md.
 *
 * Game side (game headers, no <3ds.h>): it reads gMain, gSave and the music
 * state, and runs inside the game's main loop. */
#include "save_state_3ds.h"

#include "backgroundAnimations.h"
#include "main.h"
#include "map.h"
#include "screen.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "port_config.h"
#include "port_audio_3ds.h"
#include "port_entity_ctx.h"
#include "port_second_screen_3ds.h"
#include "state_pointers_3ds.h"
#include "state_symbols_3ds.h"
#include "tmc3ds_version.h"
#include "retro_ach.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* 3dsx.ld: the game's code and constants, data and .bss. */
extern char __ss_text_start[], __ss_text_end[], __ss_rodata_start[], __ss_rodata_end[];
extern char __ss_data_start[], __ss_data_end[], __ss_bss_start[], __ss_bss_end[];
extern void Port_Widescreen_ResetAfterStateLoad(void); /* port_linked_stubs.c */
extern u8* gRomData;
extern u32 gRomSize;

/* platform_3ds.c: the top screen as shown, scaled to w x h RGB565. */
extern bool Platform3DS_CaptureTopThumbnail(uint16_t* pixels, int width, int height);

#define STATES_DIR "states"
#define STATE_MAGIC "TMC3DSST"
#define STATE_VERSION 1u /* a raw copy of the game's .data/.bss: loads only into the build that wrote it */
#define STATE_VERSION_NAMED 2u /* globals by name and pointers as relocations: loads into any build */
#define STATE_END_MARK 0x21444E45u /* "END!" */
#define THUMB_MAGIC 0x314D4854u /* "THM1" */

typedef struct StateHeader {
    char magic[8];
    uint32_t version;
    uint32_t fingerprint;  /* GameFingerprint(): the game's layout and code */
    uint32_t romIdentity;  /* region and size of the ROM it belongs to */
    uint32_t romBase;      /* where the ROM image was; pointers into it are moved on load */
    uint32_t dataSize, bssSize;
    uint32_t savedAt;
    uint8_t area, room, health, maxHealth;
    uint16_t rupees;
    uint16_t reserved;
    char build[24];
} StateHeader;

static const char* const kAreaNames[] = {
    "MINISH WOODS",
    "MINISH VILLAGE",
    "HYRULE TOWN",
    "HYRULE FIELD",
    "CASTOR WILDS",
    "RUINS",
    "MT CRENEL",
    "CASTLE GARDEN",
    "CLOUD TOPS",
    "ROYAL VALLEY",
    "VEIL FALLS",
    "LAKE HYLIA",
    "LAKE WOODS CAVE",
    "BEANSTALKS",
    "EMPTY",
    "HYRULE DIG CAVES",
    "MELARIS MINE",
    "MINISH PATHS",
    "CRENEL MINISH PATHS",
    "DIG CAVES",
    "CRENEL DIG CAVE",
    "FESTIVAL TOWN",
    "VEIL FALLS DIG CAVE",
    "CASTOR WILDS DIG CAVE",
    "OUTER FORTRESS OF WINDS",
    "HYLIA DIG CAVES",
    "VEIL FALLS TOP",
    "NULL 1B",
    "NULL 1C",
    "NULL 1D",
    "NULL 1E",
    "NULL 1F",
    "MINISH HOUSE INTERIORS",
    "HOUSE INTERIORS 1",
    "HOUSE INTERIORS 2",
    "HOUSE INTERIORS 3",
    "TREE INTERIORS",
    "DOJOS",
    "CRENEL CAVES",
    "MINISH CRACKS",
    "HOUSE INTERIORS 4",
    "GREAT FAIRIES",
    "CASTOR CAVES",
    "CASTOR DARKNUT",
    "ARMOS INTERIORS",
    "TOWN MINISH HOLES",
    "MINISH RAFTERS",
    "GORON CAVE",
    "WIND TRIBE TOWER",
    "WIND TRIBE TOWER ROOF",
    "CAVES",
    "VEIL FALLS CAVES",
    "ROYAL VALLEY GRAVES",
    "MINISH CAVES",
    "CASTLE GARDEN MINISH HOLES",
    "37",
    "EZLO CUTSCENE",
    "NULL 39",
    "NULL 3A",
    "NULL 3B",
    "NULL 3C",
    "NULL 3D",
    "NULL 3E",
    "NULL 3F",
    "40",
    "HYRULE TOWN UNDERGROUND",
    "GARDEN FOUNTAINS",
    "HYRULE CASTLE CELLAR",
    "SIMONS SIMULATION",
    "45",
    "NULL 46",
    "47",
    "DEEPWOOD SHRINE",
    "DEEPWOOD SHRINE BOSS",
    "DEEPWOOD SHRINE ENTRY",
    "NULL 4B",
    "NULL 4C",
    "4D",
    "NULL 4E",
    "NULL 4F",
    "CAVE OF FLAMES",
    "CAVE OF FLAMES BOSS",
    "NULL 52",
    "NULL 53",
    "NULL 54",
    "NULL 55",
    "NULL 56",
    "57",
    "FORTRESS OF WINDS",
    "FORTRESS OF WINDS TOP",
    "INNER MAZAAL",
    "NULL 5B",
    "NULL 5C",
    "NULL 5D",
    "NULL 5E",
    "5F",
    "TEMPLE OF DROPLETS",
    "NULL 61",
    "HYRULE TOWN MINISH CAVES",
    "NULL 63",
    "NULL 64",
    "NULL 65",
    "NULL 66",
    "67",
    "ROYAL CRYPT",
    "NULL 69",
    "NULL 6A",
    "NULL 6B",
    "NULL 6C",
    "NULL 6D",
    "NULL 6E",
    "6F",
    "PALACE OF WINDS",
    "PALACE OF WINDS BOSS",
    "NULL 72",
    "NULL 73",
    "NULL 74",
    "NULL 75",
    "NULL 76",
    "77",
    "SANCTUARY",
    "NULL 79",
    "NULL 7A",
    "NULL 7B",
    "NULL 7C",
    "NULL 7D",
    "NULL 7E",
    "7F",
    "HYRULE CASTLE",
    "SANCTUARY ENTRANCE",
    "NULL 82",
    "NULL 83",
    "NULL 84",
    "NULL 85",
    "NULL 86",
    "87",
    "DARK HYRULE CASTLE",
    "DARK HYRULE CASTLE OUTSIDE",
    "VAATIS ARMS",
    "VAATI 3",
    "VAATI 2",
    "DARK HYRULE CASTLE BRIDGE",
    "NULL 8E",
    "8F",
    "90",
    "91",
    "92",
    "93",
    "94",
    "95",
    "96",
    "97",
    "98",
};

const char* SaveState_AreaName(unsigned area) {
    return area < sizeof(kAreaNames) / sizeof(kAreaNames[0]) ? kAreaNames[area] : NULL;
}

/* ---- the list, newest first; written by the main thread only ----
 * Readers on other threads copy it under a sequence counter (odd while it
 * changes) instead of a lock: this file cannot reach the 3DS primitives. */

enum { MAX_STATES = 256 };
static SaveStateInfo sList[MAX_STATES];
static int sCount;
static volatile uint32_t sListSeq;
static bool sScanned;

static void ListBegin(void) { __atomic_add_fetch(&sListSeq, 1, __ATOMIC_ACQ_REL); }
static void ListEnd(void) { __atomic_add_fetch(&sListSeq, 1, __ATOMIC_ACQ_REL); }

int SaveState_Count(void) {
    return __atomic_load_n(&sCount, __ATOMIC_ACQUIRE);
}

bool SaveState_InfoAt(int index, SaveStateInfo* out) {
    for (int tries = 0; tries < 100; ++tries) {
        const uint32_t seq = __atomic_load_n(&sListSeq, __ATOMIC_ACQUIRE);
        if (seq & 1u) continue;
        const int count = sCount;
        const bool ok = index >= 0 && index < count;
        if (ok) *out = sList[index];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&sListSeq, __ATOMIC_ACQUIRE) == seq) return ok;
    }
    return false;
}

static int FindIndex(uint32_t id) {
    for (int i = 0; i < sCount; ++i)
        if (sList[i].id == id) return i;
    return -1;
}

static int CompareNewest(const void* a, const void* b) {
    const SaveStateInfo* x = a;
    const SaveStateInfo* y = b;
    if (x->savedAt != y->savedAt) return x->savedAt < y->savedAt ? 1 : -1;
    return x->id < y->id ? 1 : (x->id > y->id ? -1 : 0);
}

static void ListUpsert(const SaveStateInfo* info) {
    ListBegin();
    int i = FindIndex(info->id);
    if (i < 0 && sCount < MAX_STATES) i = sCount++;
    if (i >= 0) {
        sList[i] = *info;
        qsort(sList, (size_t)sCount, sizeof(sList[0]), CompareNewest);
    }
    ListEnd();
}

static void ListRemove(uint32_t id) {
    ListBegin();
    const int i = FindIndex(id);
    if (i >= 0) {
        memmove(&sList[i], &sList[i + 1], (size_t)(sCount - i - 1) * sizeof(sList[0]));
        --sCount;
    }
    ListEnd();
}

static unsigned Number(uint32_t id) {
    const int i = FindIndex(id);
    return i < 0 ? 0u : (unsigned)i + 1u;
}

/* ---- jobs ---- */

static volatile uint32_t sPending; /* SaveStateJob, NONE when free */
static uint32_t sPendingId;
static volatile uint32_t sGeneration;
static volatile uint32_t sNoticeGeneration; /* the bottom paint that shows the job's notice */
static char sMessage[48];
static bool sLastOk;
static uint32_t sLastId;

enum { JOB_CLAIMED = 0xFF }; /* taken by a requester still writing the id */

bool SaveState_Request(SaveStateJob job, uint32_t id) {
    uint32_t expected = SAVE_STATE_JOB_NONE;
    if (!__atomic_compare_exchange_n(&sPending, &expected, JOB_CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    sPendingId = id;
    __atomic_store_n(&sPending, (uint32_t)job, __ATOMIC_RELEASE);
    /* The paint after this one shows the busy notice (it reads the pending job). */
    __atomic_store_n(&sNoticeGeneration, Port_SecondScreen_3DS_RequestRefresh(), __ATOMIC_RELEASE);
    return true;
}

bool SaveState_Busy(void) {
    return __atomic_load_n(&sPending, __ATOMIC_ACQUIRE) != SAVE_STATE_JOB_NONE;
}

SaveStateJob SaveState_PendingJob(void) {
    const uint32_t job = __atomic_load_n(&sPending, __ATOMIC_ACQUIRE);
    return job == JOB_CLAIMED ? SAVE_STATE_JOB_NONE : (SaveStateJob)job;
}

uint32_t SaveState_Generation(void) {
    return __atomic_load_n(&sGeneration, __ATOMIC_ACQUIRE);
}

bool SaveState_LastResult(char* message, unsigned cap, uint32_t* id) {
    if (message) snprintf(message, cap, "%s", sMessage);
    if (id) *id = sLastId;
    return sLastOk;
}

bool SaveState_CanSave(void) {
    return gMain.task == TASK_GAME;
}

static void Finish(bool ok, uint32_t id, const char* fmt, unsigned number) {
    snprintf(sMessage, sizeof(sMessage), fmt, number);
    sLastOk = ok;
    sLastId = id;
}

/* ---- identity ---- */

static uint32_t Fnv(uint32_t h, const void* data, size_t size) {
    const uint8_t* p = data;
    while (size--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

/* Where 3dsx.ld put the game's code, constants, data and .bss. Any change to
 * the game's sources moves at least one of these ends, and then the pointers
 * in a state would no longer match: such a build refuses it. Not the bytes of
 * the game's code: they hold the addresses of port functions it calls, which
 * move with every port change without moving anything a state points to. */
static uint32_t GameFingerprint(void) {
    static uint32_t cached;
    if (cached) return cached;
    const uintptr_t marks[] = {
        (uintptr_t)__ss_text_start, (uintptr_t)__ss_text_end, (uintptr_t)__ss_rodata_start,
        (uintptr_t)__ss_rodata_end, (uintptr_t)__ss_data_start, (uintptr_t)__ss_data_end,
        (uintptr_t)__ss_bss_start,  (uintptr_t)__ss_bss_end,
    };
    const uint32_t h = Fnv(2166136261u, marks, sizeof(marks));
    cached = h ? h : 1u;
    return cached;
}

static uint32_t RomIdentity(void) {
    return ((uint32_t)gRomRegion + 1u) * 0x01000193u ^ gRomSize;
}

static bool HeaderLoadable(const StateHeader* h) {
    if (h->version == STATE_VERSION_NAMED) return h->romIdentity == RomIdentity() && StateSymbols_Ready();
    return h->version == STATE_VERSION && h->fingerprint == GameFingerprint() && h->romIdentity == RomIdentity() &&
           h->dataSize == (uint32_t)(__ss_data_end - __ss_data_start) &&
           h->bssSize == (uint32_t)(__ss_bss_end - __ss_bss_start);
}

/* ---- files ---- */

static void StatePath(char* out, size_t cap, uint32_t id) {
    snprintf(out, cap, STATES_DIR "/tmc-state%u.bin", (unsigned)id);
}

static void ThumbPath(char* out, size_t cap, uint32_t id) {
    snprintf(out, cap, STATES_DIR "/tmc-state%u.thm", (unsigned)id);
}

static void InfoFromHeader(uint32_t id, const StateHeader* h, SaveStateInfo* info) {
    memset(info, 0, sizeof(*info));
    info->id = id;
    info->savedAt = h->savedAt;
    info->area = h->area;
    info->room = h->room;
    info->health = h->health;
    info->maxHealth = h->maxHealth;
    info->rupees = h->rupees;
    info->compatible = HeaderLoadable(h);
    memcpy(info->build, h->build, sizeof(info->build));
    info->build[sizeof(info->build) - 1] = 0;
    char path[64];
    ThumbPath(path, sizeof(path), id);
    struct stat st;
    info->hasThumb = stat(path, &st) == 0;
}

static bool ReadHeader(uint32_t id, StateHeader* h) {
    char path[64];
    StatePath(path, sizeof(path), id);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    const bool ok = fread(h, sizeof(*h), 1, f) == 1 && !memcmp(h->magic, STATE_MAGIC, 8);
    fclose(f);
    return ok;
}

static void Scan(void) {
    ListBegin();
    sCount = 0;
    ListEnd();
    DIR* dir = opendir(STATES_DIR);
    if (dir) {
        struct dirent* e;
        while ((e = readdir(dir)) != NULL) {
            unsigned id = 0;
            char tail[8] = "";
            if (sscanf(e->d_name, "tmc-state%u%7s", &id, tail) != 2 || strcmp(tail, ".bin") || id == 0) continue;
            StateHeader h;
            if (!ReadHeader(id, &h)) continue;
            SaveStateInfo info;
            InfoFromHeader(id, &h, &info);
            ListUpsert(&info);
        }
        closedir(dir);
    }
    sScanned = true;
}

bool SaveState_ReadThumb(uint32_t id, uint16_t* pixels) {
    char path[64];
    ThumbPath(path, sizeof(path), id);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t head[2] = { 0, 0 };
    const size_t n = (size_t)SAVE_STATE_THUMB_W * SAVE_STATE_THUMB_H;
    const bool ok = fread(head, sizeof(head), 1, f) == 1 && head[0] == THUMB_MAGIC &&
                    head[1] == ((uint32_t)SAVE_STATE_THUMB_W << 16 | SAVE_STATE_THUMB_H) &&
                    fread(pixels, sizeof(uint16_t), n, f) == n;
    fclose(f);
    return ok;
}

static bool WriteThumb(uint32_t id, const uint16_t* pixels) {
    char path[64];
    ThumbPath(path, sizeof(path), id);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t head[2] = { THUMB_MAGIC, (uint32_t)SAVE_STATE_THUMB_W << 16 | SAVE_STATE_THUMB_H };
    const size_t n = (size_t)SAVE_STATE_THUMB_W * SAVE_STATE_THUMB_H;
    bool ok = fwrite(head, sizeof(head), 1, f) == 1 && fwrite(pixels, sizeof(uint16_t), n, f) == n;
    if (fclose(f) != 0) ok = false;
    if (!ok) remove(path);
    return ok;
}

/* ---- the named format (version 2) ----
 *
 *   header
 *   u32 n, then n x { u8 nameLen, name, u32 size, bytes }: the game's globals
 *   u32 n, then n relocations: u8 kind, u8 len + holder, u32 wordOff, then
 *       VAR: u8 len + target name, u32 targetOff
 *       FUNC: u32 hash, u32 targetOff
 *   u32 END mark
 *
 * Nothing in it depends on where a build put things: only on the ROM. */

static bool PutU8(FILE* f, uint8_t v) { return fwrite(&v, 1, 1, f) == 1; }
static bool PutU32(FILE* f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1; }
static bool PutName(FILE* f, const char* name) {
    const size_t n = strlen(name);
    return n < 256 && PutU8(f, (uint8_t)n) && fwrite(name, 1, n, f) == n;
}

static bool WriteNamedBody(FILE* f) {
    uint32_t count = 0;
    const int nVars = StateVars_Count();
    for (int i = 0; i < nVars; ++i)
        if (!StatePointers_Excluded(StateVars_At(i)->name)) ++count;
    bool ok = PutU32(f, count);
    for (int i = 0; i < nVars && ok; ++i) {
        const StateVar* v = StateVars_At(i);
        if (StatePointers_Excluded(v->name)) continue;
        ok = PutName(f, v->name) && PutU32(f, v->size) && fwrite(v->ptr, 1, v->size, f) == v->size;
    }
    StateReloc* rel = NULL;
    const int nRel = StatePointers_Collect(&rel);
    ok = ok && PutU32(f, (uint32_t)nRel);
    for (int i = 0; i < nRel && ok; ++i) {
        const StateReloc* r = &rel[i];
        ok = PutU8(f, r->kind) && PutName(f, r->holder) && PutU32(f, r->wordOff) && PutName(f, r->target) &&
             PutU32(f, r->targetOff);
    }
    free(rel);
    return ok && PutU32(f, STATE_END_MARK);
}

typedef struct Cursor {
    const uint8_t* p;
    const uint8_t* end;
    bool bad;
} Cursor;

static const uint8_t* Take(Cursor* c, size_t n) {
    if (c->bad || (size_t)(c->end - c->p) < n) {
        c->bad = true;
        return NULL;
    }
    const uint8_t* r = c->p;
    c->p += n;
    return r;
}

static uint32_t GetU32(Cursor* c) {
    uint32_t v = 0;
    const uint8_t* p = Take(c, 4);
    if (p) memcpy(&v, p, 4);
    return v;
}

static uint8_t GetU8(Cursor* c) {
    const uint8_t* p = Take(c, 1);
    return p ? *p : 0;
}

static void GetName(Cursor* c, char* out, size_t cap) {
    const size_t n = GetU8(c);
    const uint8_t* p = Take(c, n);
    if (!p || n >= cap) {
        c->bad = true;
        out[0] = 0;
        return;
    }
    memcpy(out, p, n);
    out[n] = 0;
}

static void GetReloc(Cursor* c, StateReloc* r) {
    memset(r, 0, sizeof(*r));
    r->kind = GetU8(c);
    GetName(c, r->holder, sizeof(r->holder));
    r->wordOff = GetU32(c);
    GetName(c, r->target, sizeof(r->target));
    r->targetOff = GetU32(c);
}

/* ---- save, load, delete ---- */

static uint16_t sThumb[SAVE_STATE_THUMB_W * SAVE_STATE_THUMB_H];

static void Save(uint32_t id, bool isNew) {
    if (!SaveState_CanSave()) {
        Finish(false, 0, "ONLY DURING A GAME", 0);
        return;
    }
    if (isNew) {
        id = 0;
        for (int i = 0; i < sCount; ++i)
            if (sList[i].id > id) id = sList[i].id;
        ++id;
    }
    /* First the picture: what was on screen when the save was asked for. */
    const bool haveThumb = Platform3DS_CaptureTopThumbnail(sThumb, SAVE_STATE_THUMB_W, SAVE_STATE_THUMB_H);

    StateHeader h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, STATE_MAGIC, 8);
    h.version = STATE_VERSION;
    h.fingerprint = GameFingerprint();
    h.romIdentity = RomIdentity();
    h.romBase = (uint32_t)(uintptr_t)gRomData;
    h.dataSize = (uint32_t)(__ss_data_end - __ss_data_start);
    h.bssSize = (uint32_t)(__ss_bss_end - __ss_bss_start);
    h.savedAt = (uint32_t)time(NULL);
    h.area = gRoomControls.area;
    h.room = gRoomControls.room;
    h.health = gSave.stats.health;
    h.maxHealth = gSave.stats.maxHealth;
    h.rupees = (uint16_t)gSave.stats.rupees;
    snprintf(h.build, sizeof(h.build), "%s", TMC_PORT_VERSION);

    const bool named = StateSymbols_Ready();
    if (named) {
        h.version = STATE_VERSION_NAMED;
        h.fingerprint = 0;
        h.dataSize = h.bssSize = 0;
    }

    mkdir(STATES_DIR, 0777);
    char path[64], tmp[72];
    StatePath(path, sizeof(path), id);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    bool ok = f != NULL;
    /* One buffer per write: the SD card is slow with many small ones. */
    if (ok) setvbuf(f, NULL, _IOFBF, 64 * 1024);
    ok = ok && fwrite(&h, sizeof(h), 1, f) == 1;
    if (ok && named) {
        ok = WriteNamedBody(f);
    } else if (ok) {
        ok = fwrite(__ss_data_start, 1, h.dataSize, f) == h.dataSize;
        ok = ok && fwrite(__ss_bss_start, 1, h.bssSize, f) == h.bssSize;
    }
    if (f && fclose(f) != 0) ok = false;
    /* Written beside the old file and swapped in: a failed save keeps it. */
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) {
        remove(tmp);
        Finish(false, id, "SAVE FAILED (SD CARD)", 0);
        return;
    }
    char tpath[64];
    ThumbPath(tpath, sizeof(tpath), id);
    if (!(haveThumb && WriteThumb(id, sThumb))) remove(tpath); /* never an old picture beside new data */

    SaveStateInfo info;
    InfoFromHeader(id, &h, &info);
    ListUpsert(&info);
    RetroAch_StateSaved(id); /* the achievements' progress goes with the state */
    Finish(true, id, "STATE %u SAVED", Number(id));
}

/* The ROM image is allocated at boot; if it sits elsewhere now, pointers to
 * it in the restored data move with it. Its address range (linear memory)
 * cannot be mistaken for the game's numbers. */
static void MoveRomPointers(uintptr_t oldBase) {
    const uintptr_t newBase = (uintptr_t)gRomData;
    if (oldBase == newBase || !oldBase) return;
    char* ranges[2][2] = { { __ss_data_start, __ss_data_end }, { __ss_bss_start, __ss_bss_end } };
    for (int r = 0; r < 2; ++r) {
        for (uint32_t* w = (uint32_t*)ranges[r][0]; (char*)(w + 1) <= ranges[r][1]; ++w) {
            if (*w >= oldBase && *w < oldBase + gRomSize) *w = (uint32_t)(*w - oldBase + newBase);
        }
    }
}

/* States written before gMapBottom/gMapTop (49 KB each, never scanned) and gScreen's inner
 * pointers were relocated hold the address gScreen had in the build that saved them. After a
 * rebuild that moved gScreen they point into the middle of a BgSettings, and every consumer
 * (the widescreen's shadows, the map layers) reads a wrong control word. The pointers the
 * game keeps there are always one of the BG settings; one that is not is put back on the
 * BG the game uses for that layer (the top layer on BG1, the bottom one on BG2). */
/* Each entity keeps its script context twice: in gEntityScriptCtxTable (a holder the state
 * relocates) and in its own scriptContext field (+0x84), which points into the middle of
 * gScriptExecutionContextArray. States written before that array was a relocation target
 * (2026-10-13) hold the field as the writing build's address; once the array has moved, an
 * entity runs a script from whatever is there now (a house door in Lon Lon Ranch: a data abort
 * in GetNextScriptCommandHalfword). Put the field back from the table, which Port_SetEntityScriptCtx
 * keeps equal to it. */
static void RepairEntityScriptContexts(void) {
    for (int slot = 1; slot < PC_MAX_ENTITY_SLOTS; ++slot) {
        ScriptExecutionContext* ctx = gEntityScriptCtxTable[slot];
        Entity* ent = Port_EntityFromSlot(slot);
        if (ctx != NULL && ent != NULL && ent->next != NULL)
            ((GenericEntity*)ent)->scriptContext = ctx;
    }
}

static void RepairScreenPointers(void) {
    BgSettings* const valid[4] = { &gScreen.bg0, &gScreen.bg1, (BgSettings*)&gScreen.bg2, (BgSettings*)&gScreen.bg3 };
    BgSettings** const holders[4] = { &gMapTop.bgSettings, &gMapBottom.bgSettings, &gUI.mapTopBgSettings,
                                      &gUI.mapBottomBgSettings };
    BgSettings* const fallback[4] = { &gScreen.bg1, (BgSettings*)&gScreen.bg2, &gScreen.bg1,
                                      (BgSettings*)&gScreen.bg2 };
    for (int i = 0; i < 4; ++i) {
        BgSettings* const p = *holders[i];
        if (p == NULL) continue;
        bool ok = false;
        for (int j = 0; j < 4; ++j) ok = ok || p == valid[j];
        if (!ok) *holders[i] = fallback[i];
    }
}

/* `f` is positioned after the header. Closes it. */
static void LoadNamed(FILE* f, const StateHeader* h, uint32_t id) {
    /* All of it into RAM and parsed before anything is applied: a short or
     * damaged file must not leave the game half restored. */
    const long here = ftell(f);
    fseek(f, 0, SEEK_END);
    const long end = ftell(f);
    fseek(f, here, SEEK_SET);
    if (here < 0 || end <= here) {
        fclose(f);
        Finish(false, id, "STATE DAMAGED", 0);
        return;
    }
    const size_t size = (size_t)(end - here);
    uint8_t* body = malloc(size);
    const bool read = body && fread(body, 1, size, f) == size;
    fclose(f);
    if (!read) {
        free(body);
        Finish(false, id, body ? "STATE DAMAGED" : "NO MEMORY", 0);
        return;
    }
    Cursor c = { body, body + size, false };
    char name[64];
    StateReloc rel;
    const uint32_t nVars = GetU32(&c);
    for (uint32_t i = 0; i < nVars && !c.bad; ++i) {
        GetName(&c, name, sizeof(name));
        Take(&c, GetU32(&c));
    }
    const uint32_t nRel = GetU32(&c);
    for (uint32_t i = 0; i < nRel && !c.bad; ++i) GetReloc(&c, &rel);
    if (c.bad || GetU32(&c) != STATE_END_MARK) {
        free(body);
        Finish(false, id, "STATE DAMAGED", 0);
        return;
    }

    Port_Audio_3DSSetPaused(true); /* the mixer reads the music players being replaced */
    c = (Cursor){ body, body + size, false };
    const uint32_t count = GetU32(&c);
    for (uint32_t i = 0; i < count; ++i) {
        GetName(&c, name, sizeof(name));
        const uint32_t n = GetU32(&c);
        const uint8_t* data = Take(&c, n);
        const StateVar* v = StateVars_Find(name);
        /* A global this build no longer has is dropped, a new one keeps its
         * live value, one whose size changed gets the part both have. */
        if (v && !StatePointers_Excluded(name)) memcpy(v->ptr, data, n < v->size ? n : v->size);
    }
    MoveRomPointers(h->romBase);
    const uint32_t relocs = GetU32(&c);
    unsigned animRelocated = 0; /* which gBgAnimations entries came back as a pointer */
    for (uint32_t i = 0; i < relocs; ++i) {
        GetReloc(&c, &rel);
        StatePointers_Apply(&rel);
        if (!strcmp(rel.holder, "gBgAnimations") && rel.wordOff / sizeof(BgAnimation) < MAX_BG_ANIMATIONS)
            animRelocated |= 1u << (rel.wordOff / sizeof(BgAnimation));
    }
    /* A state saved before the frame lists were relocated holds the addresses of the build that
     * wrote it; after a rebuild they point at other constants and UpdateBgAnimations uploads
     * garbage (a data abort). An entry that came back raw is ended: the list stops there and
     * the room's animations start again at the next room load. */
    for (unsigned i = 0; i < MAX_BG_ANIMATIONS; ++i) {
        if (gBgAnimations[i].currentFrame != NULL && !(animRelocated & (1u << i))) {
            for (unsigned j = i; j < MAX_BG_ANIMATIONS; ++j) gBgAnimations[j].currentFrame = NULL;
            break;
        }
    }
    free(body);
    Port_Audio_3DSSetPaused(false);
    RepairScreenPointers();
    RepairEntityScriptContexts();
    /* The players' state came back with everything else; start the saved
     * room music afresh so the mixer and the game agree on it. */
    const u32 bgm = gSoundPlayingInfo.currentBgm;
    SoundReq(SONG_STOP_ALL);
    if (bgm) SoundReq(bgm);
    Port_Widescreen_ResetAfterStateLoad();
    RetroAch_StateLoaded(id);
    Finish(true, id, "STATE %u LOADED", Number(id));
}

static void Load(uint32_t id) {
    char path[64];
    StatePath(path, sizeof(path), id);
    FILE* f = fopen(path, "rb");
    if (!f) {
        Finish(false, id, "STATE NOT FOUND", 0);
        return;
    }
    StateHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, STATE_MAGIC, 8)) {
        fclose(f);
        Finish(false, id, "NOT A SAVE STATE", 0);
        return;
    }
    if (!HeaderLoadable(&h)) {
        fclose(f);
        Finish(false, id, h.romIdentity != RomIdentity() ? "STATE OF ANOTHER ROM" : "STATE OF ANOTHER BUILD", 0);
        return;
    }
    if (h.version == STATE_VERSION_NAMED) {
        LoadNamed(f, &h, id);
        return;
    }
    /* All of it into RAM first: a short file must not leave the game half
     * restored. */
    const size_t size = (size_t)h.dataSize + h.bssSize;
    char* body = malloc(size);
    const bool read = body && fread(body, 1, size, f) == size;
    fclose(f);
    if (!read) {
        free(body);
        Finish(false, id, body ? "STATE DAMAGED" : "NO MEMORY", 0);
        return;
    }
    Port_Audio_3DSSetPaused(true); /* the mixer reads the music players being replaced */
    memcpy(__ss_data_start, body, h.dataSize);
    memcpy(__ss_bss_start, body + h.dataSize, h.bssSize);
    free(body);
    MoveRomPointers(h.romBase);
    Port_Audio_3DSSetPaused(false);
    /* The players' state came back with everything else; start the saved
     * room music afresh so the mixer and the game agree on it. */
    const u32 bgm = gSoundPlayingInfo.currentBgm;
    SoundReq(SONG_STOP_ALL);
    if (bgm) SoundReq(bgm);
    Port_Widescreen_ResetAfterStateLoad();
    RetroAch_StateLoaded(id);
    Finish(true, id, "STATE %u LOADED", Number(id));
}

static void Delete(uint32_t id) {
    const unsigned number = Number(id);
    char path[64];
    StatePath(path, sizeof(path), id);
    struct stat st;
    const bool ok = remove(path) == 0 || stat(path, &st) != 0;
    ThumbPath(path, sizeof(path), id);
    remove(path);
    if (ok) ListRemove(id);
    if (ok) RetroAch_StateDeleted(id);
    Finish(ok, id, ok ? "STATE %u DELETED" : "DELETE FAILED", number);
}

void SaveState_Service(void) {
    const uint32_t job = __atomic_load_n(&sPending, __ATOMIC_ACQUIRE);
    if (job == SAVE_STATE_JOB_NONE || job == JOB_CLAIMED) return;
    /* A job that stops the game for a while waits until the bottom screen shows its notice:
     * nothing is drawn while it runs. A fixed tenth of a second was shorter than a slow paint,
     * so the notice came up only after the job, for a frame. At most a second. */
    static unsigned sPreroll;
    if (job != SAVE_STATE_JOB_SCAN && sPreroll < 60u &&
        !Port_SecondScreen_3DS_IsShown(__atomic_load_n(&sNoticeGeneration, __ATOMIC_ACQUIRE))) {
        ++sPreroll;
        return;
    }
    sPreroll = 0;
    const uint32_t id = sPendingId;
    if (!sScanned || job == SAVE_STATE_JOB_SCAN) Scan();
    switch (job) {
        case SAVE_STATE_JOB_SCAN: Finish(true, 0, "STATES READ", 0); break;
        case SAVE_STATE_JOB_SAVE_NEW: Save(0, true); break;
        case SAVE_STATE_JOB_OVERWRITE: Save(id, false); break;
        case SAVE_STATE_JOB_LOAD: Load(id); break;
        case SAVE_STATE_JOB_DELETE: Delete(id); break;
        default: break;
    }
    __atomic_add_fetch(&sGeneration, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&sPending, SAVE_STATE_JOB_NONE, __ATOMIC_RELEASE);
}
