#include "debug_link_3ds.h"

#include "debug_files_3ds.h"
#include "debug_log_3ds.h"
#include "debug_switches_3ds.h"
#include "platform_3ds.h"
#include "port_second_screen_3ds.h"
#include "port_second_screen_state.h"
#include "save_state_3ds.h"
#include "port_ppu_gpu_3ds.h"

#include <3ds.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* port_dump_state*.h pull in the game's types, which clash with <3ds.h>.
 * PortDumpStateResult is an enum: 0 and 1 are the two kinds of success. */
extern int Port_DumpState3DS_LoadLatest(void);
extern const char* Port_DumpState_ResultLabel(int result);

/* A request the main thread never takes (game hung). Not an enum: 5e9 does
 * not fit in an int, and the truncated value gave up after 0.7 s. */
#define LINK_WAIT_NS 5000000000LL

enum {
    LINK_PEEK_MAX = 256,
    LINK_RESULT_MAX = 600,
};

typedef enum LinkOp {
    LINK_OP_NONE,
    LINK_OP_TOUCH,
    LINK_OP_SHOT,
    LINK_OP_LOADSTATE,
    LINK_OP_DUMP,
    LINK_OP_SWITCH,
    LINK_OP_POKE,
} LinkOp;

/* One request at a time: the FTP server serves one client and one command. */
static LightLock sLock = 1; /* LightLock_Init's value, so no init call is needed */
static LightEvent sDone;
static bool sDoneReady;
static volatile LinkOp sOp;
static int sArgA, sArgB;
static uintptr_t sPokeAddress;
static uint8_t sPokeBytes[LINK_PEEK_MAX];
static unsigned sPokeLength;
static char sResult[LINK_RESULT_MAX];

/* Injected buttons, written by the FTP thread, read by the main thread. */
static volatile uint32_t sKeysMask;
static volatile uint32_t sKeysFrames;   /* logic frames still to hold, set by KEYS */
static volatile bool sKeysNew;          /* KEYS arrived: start counting from the next poll */
static uint64_t sKeysUntil;
static uint32_t sKeysWasHeld;

static const struct {
    const char* name;
    uint32_t mask;
} kKeys[] = {
    { "A", KEY_A },         { "B", KEY_B },           { "X", KEY_X },        { "Y", KEY_Y },
    { "L", KEY_L },         { "R", KEY_R },           { "ZL", KEY_ZL },      { "ZR", KEY_ZR },
    { "START", KEY_START }, { "SELECT", KEY_SELECT }, { "UP", KEY_DUP },     { "DOWN", KEY_DDOWN },
    { "LEFT", KEY_DLEFT },  { "RIGHT", KEY_DRIGHT },
};

static bool ParseKeys(const char* text, uint32_t* mask) {
    *mask = 0;
    char word[16];
    while (*text && *text != ' ') {
        unsigned n = 0;
        while (*text && *text != '+' && *text != ' ' && n + 1 < sizeof(word))
            word[n++] = (char)toupper((unsigned char)*text++);
        word[n] = 0;
        if (*text == '+') ++text;
        if (!strcmp(word, "NONE")) continue;
        bool found = false;
        for (unsigned i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
            if (!strcmp(word, kKeys[i].name)) {
                *mask |= kKeys[i].mask;
                found = true;
            }
        }
        if (!found) return false;
    }
    return true;
}

/* Readable (or writable) for `length` bytes, page by page: a bad address
 * from the PC must not crash the game. */
static bool MemoryOk(uintptr_t address, unsigned length, bool write) {
    uintptr_t at = address;
    const uintptr_t end = address + length;
    while (at < end) {
        MemInfo info;
        PageInfo page;
        if (R_FAILED(svcQueryMemory(&info, &page, (u32)at))) return false;
        if (info.state == MEMSTATE_FREE) return false;
        if (!(info.perm & (write ? MEMPERM_WRITE : MEMPERM_READ))) return false;
        at = info.base_addr + info.size;
    }
    return true;
}

static int ParseHex(const char* text, uint8_t* out, unsigned cap) {
    unsigned n = 0;
    while (text[0] && text[1] && n < cap) {
        char pair[3] = { text[0], text[1], 0 };
        char* end;
        long v = strtol(pair, &end, 16);
        if (*end) return -1;
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return text[0] ? -1 : (int)n;
}

/* Hands the request to the main thread and waits for its result line. */
static void RunOnMainThread(LinkOp op, char* reply, unsigned cap) {
    LightLock_Lock(&sLock);
    if (!sDoneReady) {
        LightEvent_Init(&sDone, RESET_ONESHOT);
        sDoneReady = true;
    }
    LightEvent_Clear(&sDone);
    sResult[0] = 0;
    __atomic_store_n(&sOp, op, __ATOMIC_RELEASE);
    if (LightEvent_WaitTimeout(&sDone, LINK_WAIT_NS) != 0) {
        __atomic_store_n(&sOp, LINK_OP_NONE, __ATOMIC_RELEASE);
        snprintf(reply, cap, "451 The game did not take the request (stopped?).");
    } else {
        snprintf(reply, cap, "%s", sResult);
    }
    LightLock_Unlock(&sLock);
}

static void FormatState(char* reply, unsigned cap) {
    Platform3DSRuntimeStats rt;
    Platform3DS_GetRuntimeStats(&rt);
    SecondScreenSnapshot snap;
    Port_SecondScreenState_Read(&snap);
    if (!snap.inGame) {
        snprintf(reply, cap, "200 frame=%llu shown=%llu ingame=0",
                 (unsigned long long)rt.logicFrames, (unsigned long long)rt.presentedFrames);
        return;
    }
    snprintf(reply, cap,
             "200 frame=%llu shown=%llu ingame=1 area=%02X room=%02X x=%ld y=%ld health=%u/%u rupees=%u",
             (unsigned long long)rt.logicFrames, (unsigned long long)rt.presentedFrames, snap.area, snap.room,
             (long)snap.playerX, (long)snap.playerY, snap.health, snap.maxHealth, snap.rupees);
}

/* Runs a save-state job and waits for the game's main loop to finish it. */
static bool RunStateJob(SaveStateJob job, uint32_t id, char* reply, unsigned cap) {
    const uint64_t give_up = svcGetSystemTick() + 30ull * SYSCLOCK_ARM11;
    while (!SaveState_Request(job, id)) {
        if (svcGetSystemTick() > give_up) {
            snprintf(reply, cap, "451 Save states busy.");
            return false;
        }
        svcSleepThread(20000000);
    }
    const uint32_t gen = SaveState_Generation();
    while (SaveState_Generation() == gen) {
        if (svcGetSystemTick() > give_up) {
            snprintf(reply, cap, "451 The game did not run the job (no game loop?).");
            return false;
        }
        svcSleepThread(20000000);
    }
    char message[48];
    uint32_t done;
    const bool ok = SaveState_LastResult(message, sizeof(message), &done);
    int number = 0;
    SaveStateInfo info;
    for (int i = 0; i < SaveState_Count(); ++i)
        if (SaveState_InfoAt(i, &info) && info.id == done) number = i + 1;
    snprintf(reply, cap, "%s %s (number %d)", ok ? "200" : "550", message, number);
    return ok;
}

/* SAVESTATE [N]: a new state, or over number N. LOADSTATE [N] and DELSTATE N:
 * N counts from 1, newest first (default 1). STATES: the list. */
static void StateCommand(const char* verb, const char* args, char* reply, unsigned cap) {
    if (!SaveState_Count() && !RunStateJob(SAVE_STATE_JOB_SCAN, 0, reply, cap)) return;
    const long n = strtol(args, NULL, 10);
    SaveStateInfo info;
    if (!strcmp(verb, "STATES")) {
        int len = snprintf(reply, cap, "200 %d:", SaveState_Count());
        for (int i = 0; i < SaveState_Count() && len + 24 < (int)cap; ++i) {
            if (!SaveState_InfoAt(i, &info)) break;
            len += snprintf(reply + len, cap - (unsigned)len, " %d=%02X/%02X%s", i + 1, info.area, info.room,
                            info.compatible ? "" : "(other build)");
        }
        return;
    }
    if (!strcmp(verb, "SAVESTATE") && n <= 0) {
        RunStateJob(SAVE_STATE_JOB_SAVE_NEW, 0, reply, cap);
        return;
    }
    const long number = n > 0 ? n : 1;
    if (!SaveState_InfoAt((int)number - 1, &info)) {
        snprintf(reply, cap, "550 No state number %ld (%d saved).", number, SaveState_Count());
        return;
    }
    const SaveStateJob job = !strcmp(verb, "SAVESTATE") ? SAVE_STATE_JOB_OVERWRITE
                             : !strcmp(verb, "LOADSTATE") ? SAVE_STATE_JOB_LOAD
                                                          : SAVE_STATE_JOB_DELETE;
    RunStateJob(job, info.id, reply, cap);
}

bool DebugLink_Command(const char* verb, const char* args, char* reply, unsigned cap) {
    if (!args) args = "";
    if (!strcmp(verb, "KEYS")) {
        uint32_t mask;
        if (!ParseKeys(args, &mask)) {
            snprintf(reply, cap, "501 KEYS <A+B+LEFT...|NONE> [frames]");
            return true;
        }
        const char* frames = strchr(args, ' ');
        long n = frames ? strtol(frames + 1, NULL, 10) : 1;
        if (n < 1) n = 1;
        sKeysMask = mask;
        sKeysFrames = (uint32_t)n;
        __atomic_store_n(&sKeysNew, true, __ATOMIC_RELEASE);
        snprintf(reply, cap, "200 Holding %s for %ld frames.", args, n);
        return true;
    }
    if (!strcmp(verb, "TOUCH")) {
        if (sscanf(args, "%d %d", &sArgA, &sArgB) != 2 || sArgA < 0 || sArgA >= 320 || sArgB < 0 || sArgB >= 240) {
            snprintf(reply, cap, "501 TOUCH <x 0-319> <y 0-239>");
            return true;
        }
        RunOnMainThread(LINK_OP_TOUCH, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SHOT")) {
        RunOnMainThread(LINK_OP_SHOT, reply, cap);
        return true;
    }
    if (!strcmp(verb, "LOADDUMP")) {
        RunOnMainThread(LINK_OP_LOADSTATE, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SAVESTATE") || !strcmp(verb, "LOADSTATE") || !strcmp(verb, "DELSTATE") ||
        !strcmp(verb, "STATES")) {
        StateCommand(verb, args, reply, cap);
        return true;
    }
    if (!strcmp(verb, "DUMP")) {
        RunOnMainThread(LINK_OP_DUMP, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SWITCH")) {
        sArgA = -1;
        for (int id = 0; id < DEBUG_SW_COUNT; ++id) {
            const char* name = DebugSwitch_Name(id);
            unsigned i = 0;
            /* "FRAME SYNC" matches FRAME_SYNC, frame-sync and frame sync. */
            while (name[i] && args[i] &&
                   (toupper((unsigned char)args[i]) == name[i] ||
                    (name[i] == ' ' && (args[i] == '_' || args[i] == '-'))))
                ++i;
            if (!name[i] && !args[i]) sArgA = id;
        }
        if (sArgA < 0) {
            snprintf(reply, cap, "501 SWITCH <name as on DEBUG -> SWITCHES>");
            return true;
        }
        RunOnMainThread(LINK_OP_SWITCH, reply, cap);
        return true;
    }
    if (!strcmp(verb, "STATE")) {
        FormatState(reply, cap);
        return true;
    }
    if (!strcmp(verb, "GPUCHECK")) {
        /* Upstream's parity check: the next frame drawn both ways, compared. */
        PortPpuGpu3DSStats before, after;
        PortPpuGpu3DS_GetStats(&before);
        PortPpuGpu3DS_RequestParityCheck();
        const uint64_t give_up = svcGetSystemTick() + 5ull * SYSCLOCK_ARM11;
        do {
            svcSleepThread(20000000);
            PortPpuGpu3DS_GetStats(&after);
        } while (after.parityChecks == before.parityChecks && svcGetSystemTick() < give_up);
        if (after.parityChecks == before.parityChecks) {
            snprintf(reply, cap, "451 No check ran (GPU renderer off, or not drawing).");
            return true;
        }
        snprintf(reply, cap, "200 differing=%llu structural=%llu failed=%llu",
                 (unsigned long long)(after.differingPixels - before.differingPixels),
                 (unsigned long long)(after.structuralPixels - before.structuralPixels),
                 (unsigned long long)(after.parityFailures - before.parityFailures));
        return true;
    }
    if (!strcmp(verb, "MARK")) {
        /* A numbered marker in the SD log, which also writes out its buffer. */
        if (DebugLog_Running()) snprintf(reply, cap, "200 MARK %d", DebugLog_Mark());
        else snprintf(reply, cap, "550 The SD log is off.");
        return true;
    }
    if (!strcmp(verb, "PEEK")) {
        unsigned long address = 0, length = 0;
        if (sscanf(args, "%lx %lu", &address, &length) != 2 || length == 0 || length > LINK_PEEK_MAX) {
            snprintf(reply, cap, "501 PEEK <hex address> <length 1-%d>", LINK_PEEK_MAX);
            return true;
        }
        if (!MemoryOk((uintptr_t)address, (unsigned)length, false)) {
            snprintf(reply, cap, "550 Not readable.");
            return true;
        }
        int n = snprintf(reply, cap, "200 ");
        const uint8_t* p = (const uint8_t*)(uintptr_t)address;
        for (unsigned long i = 0; i < length && (unsigned)n + 3 < cap; ++i)
            n += snprintf(reply + n, cap - (unsigned)n, "%02x", p[i]);
        return true;
    }
    if (!strcmp(verb, "POKE")) {
        unsigned long address = 0;
        char hex[2 * LINK_PEEK_MAX + 1];
        int length;
        if (sscanf(args, "%lx %512s", &address, hex) != 2 ||
            (length = ParseHex(hex, sPokeBytes, LINK_PEEK_MAX)) <= 0) {
            snprintf(reply, cap, "501 POKE <hex address> <hex bytes>");
            return true;
        }
        if (!MemoryOk((uintptr_t)address, (unsigned)length, true)) {
            snprintf(reply, cap, "550 Not writable.");
            return true;
        }
        sPokeAddress = (uintptr_t)address;
        sPokeLength = (unsigned)length;
        RunOnMainThread(LINK_OP_POKE, reply, cap);
        return true;
    }
    return false;
}

static void RunRequest(LinkOp op) {
    switch (op) {
        case LINK_OP_TOUCH:
            Port_SecondScreen_3DS_OnTap(sArgA, sArgB, 0);
            snprintf(sResult, sizeof(sResult), "200 Tapped %d,%d.", sArgA, sArgB);
            break;
        case LINK_OP_SHOT: {
            mkdir(DEBUG_FILES_DIR, 0777);
            const bool ok = Platform3DS_SaveDisplayedScreens(DEBUG_FILES_DIR "/shot-top.bmp",
                                                             DEBUG_FILES_DIR "/shot-bottom.bmp");
            snprintf(sResult, sizeof(sResult), ok ? "200 debug/shot-top.bmp debug/shot-bottom.bmp"
                                                  : "550 Could not write the screenshots.");
            break;
        }
        case LINK_OP_LOADSTATE: {
            const int result = Port_DumpState3DS_LoadLatest();
            snprintf(sResult, sizeof(sResult), "%s %s", result <= 1 ? "200" : "550",
                     Port_DumpState_ResultLabel(result));
            break;
        }
        case LINK_OP_DUMP: {
            extern void Port_PPU_3DS_WriteQuickDump(void);
            extern const char* Port_PPU_3DS_LastDumpDirectory(void);
            Port_PPU_3DS_WriteQuickDump();
            snprintf(sResult, sizeof(sResult), "200 %s", Port_PPU_3DS_LastDumpDirectory());
            break;
        }
        case LINK_OP_SWITCH: {
            DebugSwitch_Cycle(sArgA);
            char value[24];
            DebugSwitch_Value(sArgA, value, sizeof(value));
            DebugLog_Printf("switch %s -> %s (link)", DebugSwitch_Name(sArgA), value);
            snprintf(sResult, sizeof(sResult), "200 %s = %s", DebugSwitch_Name(sArgA), value);
            break;
        }
        case LINK_OP_POKE:
            memcpy((void*)sPokeAddress, sPokeBytes, sPokeLength);
            snprintf(sResult, sizeof(sResult), "200 Wrote %u bytes.", sPokeLength);
            break;
        default: return;
    }
}

void DebugLink_Poll(uint32_t* held, uint32_t* down, uint64_t logicFrame) {
    if (__atomic_exchange_n(&sKeysNew, false, __ATOMIC_ACQ_REL)) {
        sKeysUntil = logicFrame + sKeysFrames;
        sKeysWasHeld = 0; /* a new KEYS presses again, even the same buttons */
    }
    uint32_t inject = logicFrame < sKeysUntil ? sKeysMask : 0;
    *held |= inject;
    *down |= inject & ~sKeysWasHeld;
    sKeysWasHeld = inject;

    const LinkOp op = __atomic_exchange_n(&sOp, LINK_OP_NONE, __ATOMIC_ACQ_REL);
    if (op != LINK_OP_NONE) {
        RunRequest(op);
        LightEvent_Signal(&sDone);
    }
}
