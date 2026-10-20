/* Host stand-ins for what the bottom-screen compositor reaches outside
 * port_second_screen*.c: the 3DS platform, the runtime config, the updater and
 * the game/ROM tables. With no ROM loaded every ROM lookup returns nothing, so
 * the theme and the map/item painters take their own fallbacks; the settings
 * and debug pages draw exactly as on the console. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "updater.h"
#include "port_ppu_gpu_3ds.h"

/* Built apart from the game headers (their declarations of the ROM symbols
 * below differ from these placeholders), so the dump enum is an int here. */
typedef int PortDumpStateResult;

/* ---- 3DS platform ---- */
bool Platform3DS_IsNew3DS(void) { return true; }
unsigned Platform3DS_Core1TimeLimit(void) { return 80; }
uint64_t Platform3DS_SystemTick(void) { return 0; }
void Port_SecondScreen_3DS_LockUI(void) {}
void Port_SecondScreen_3DS_UnlockUI(void) {}
bool Platform3DS_BatteryStatus(uint8_t* level, uint8_t* charging) { *level = 4; *charging = 0; return true; }
int Platform3DS_WifiStrength(void) { return 2; }
double Port_PPU_3DS_CurrentFps(void) { return 59.8; }
double Port_PPU_3DS_AverageFps(void) { return 58.4; }
void Platform3DS_RequestScreenDump(void) {}
void Port_PPU_3DS_RequestGpuCheck(void) {}
void Port_PPU_SetColorCorrection(bool on) { (void)on; }
const char* Port_DumpState_ResultLabel(PortDumpStateResult r) { (void)r; return "OK"; }
void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) { (void)itemId; (void)slot; }

/* ---- runtime config: plain variables ---- */
static int sUiTheme = 0, sPacing = 0;
int Port_Config_GetFramePacing(void) { return sPacing; }
void Port_Config_SetFramePacing(int p) { sPacing = p; }
int Port_Config_GetUiTheme(void) { return sUiTheme; }
void Port_Config_SetUiTheme(int theme) { sUiTheme = theme; }
static bool sTabsBottom;
bool Port_Config_GetUiTabsBottom(void) { return sTabsBottom; }
void Port_Config_SetUiTabsBottom(bool bottom) { sTabsBottom = bottom; }
static int sAspect = 0, sStyle = 2, sBackdrop = 0;
static bool sWideAi = false, sWideAiAsked = false;
bool Port_Config_GetWideAi(void) { return sWideAi; }
bool Port_Config_WideAiQuestionDue(void) { return !sWideAiAsked && !getenv("NO_WIDE_ASK"); }
void Port_Config_AnswerWideAi(bool yes) { sWideAi = yes; sWideAiAsked = true; }
static bool sColor = true, sHideHud = false, sHold = true, sCrests = true, sFloor = true, sFollow = true;
static bool sSwap = false, sFps = false, sWide = true, sAuto = true;
static unsigned sTurbo = 5;
static float sVolume = 1.0f;
static const char* const kAspect[] = { "WIDE", "ORIGINAL" };
static const char* const kStyle[] = { "BLUR", "BILINEAR", "PIXEL PERFECT" };
void Port_Config_Cycle3DSAspectRatio(void) { sAspect = (sAspect + 1) % 2; }
void Port_Config_Cycle3DSDisplayStyle(void) { sStyle = (sStyle + 1) % 3; }
const char* Port_Config_Get3DSAspectRatioName(void) { return kAspect[sAspect]; }
int Port_Config_Get3DSDisplayStyle(void) { return sStyle; }
const char* Port_Config_Get3DSDisplayStyleName(void) { return kStyle[sStyle]; }
static int sGrid = 0;
static int sGbaFrame = 0;
static const char* const kGrid[] = { "OFF", "LOW", "MED", "HIGH" };
int Port_Config_GetGbaGrid(void) { return sGrid; }
const char* Port_Config_GetGbaGridName(void) { return kGrid[sGrid]; }
void Port_Config_CycleGbaGrid(void) { sGrid = (sGrid + 1) % 4; }
static int sNeighbourRooms = 1;
int Port_Config_GetNeighbourRooms(void) { return sNeighbourRooms; }
const char* Port_Config_GetNeighbourRoomsName(void) { return sNeighbourRooms ? "ON" : "OFF"; }
void Port_Config_CycleNeighbourRooms(void) { sNeighbourRooms = !sNeighbourRooms; }
int Port_Config_GetGbaFrame(void) { return sGbaFrame; }
const char* Port_Config_GetGbaFrameName(void) { static const char* const n[] = { "NO", "GBA", "BLUR" }; return n[sGbaFrame]; }
bool Port_Config_GbaFrameAvailable(void) { return sAspect == 1; }
int Port_Config_GbaFrameActive(void) { return sAspect == 1 ? sGbaFrame : 0; }
void Port_Config_CycleGbaFrame(void) { sGbaFrame = (sGbaFrame + 1) % 3; }
bool Port_Config_GetColorCorrection(void) { return sColor; }
int sTopHudMode = 0;
int Port_Config_GetTopHudMode(void) { return sTopHudMode; }
void Port_Config_CycleTopHudMode(void) { sTopHudMode = (sTopHudMode + 1) % 3; }
bool Port_Config_GetHideTopHud(void) { return sHideHud; }
bool Port_Config_GetHoldToAdvanceText(void) { return sHold; }
int Port_Config_GetSecondScreenBackdrop(void) { return sBackdrop; }
bool Port_Config_GetSecondScreenCrestPins(void) { return sCrests; }
bool Port_Config_GetSecondScreenFloorReturn(void) { return sFloor; }
bool Port_Config_GetSecondScreenFollowCam(void) { return sFollow; }
bool Port_Config_GetSecondScreenSwap(void) { return sSwap; }
bool Port_Config_GetShowFps(void) { return sFps; }
unsigned Port_Config_GetTurboMultiplier(void) { return sTurbo; }
void Port_Config_SetAutosaveEnabled(bool on) { sAuto = on; }
void Port_Config_SetColorCorrection(bool on) { sColor = on; }
void Port_Config_SetHideTopHud(bool on) { sHideHud = on; }
void Port_Config_SetHoldToAdvanceText(bool on) { sHold = on; }
void Port_Config_SetMasterVolume(float v) { sVolume = v; }
void Port_Config_SetSecondScreenBackdrop(int style) { sBackdrop = style; }
void Port_Config_SetSecondScreenCrestPins(bool on) { sCrests = on; }
void Port_Config_SetSecondScreenFloorReturn(bool on) { sFloor = on; }
void Port_Config_SetSecondScreenFollowCam(bool on) { sFollow = on; }
void Port_Config_SetSecondScreenSwap(bool on) { sSwap = on; }
void Port_Config_SetShowFps(bool on) { sFps = on; }
void Port_Config_SetTurboMultiplier(unsigned m) { sTurbo = m; }
void Port_Config_SetWidescreenEnabled(bool on) { sWide = on; }
bool Port_Config_WidescreenEnabled(void) { return sWide; }
float Port_Audio_GetMasterVolume(void) { return sVolume; }
void Port_Audio_SetMasterVolume(float v) { sVolume = v; }
int Port_QuickSave_AutoEnabled(void) { return sAuto; }
void Port_QuickSave_SetAutoEnabled(int on) { sAuto = on != 0; }

/* ---- updater ---- */
bool Updater_Busy(void) { return false; }
void Updater_Cancel(void) {}
void Updater_Check(void) {}
void Updater_Download(void) {}
unsigned Platform3DS_RelaunchRemainingMs(void) { return 4000; }
/* UPDATE_PROMPT=ask|work|restart|error: the prompt raised by the boot check, to preview its window. */
UpdatePrompt Updater_AutoPrompt(void) {
    const char* p = getenv("UPDATE_PROMPT");
    return !p ? UPDATE_PROMPT_NONE : p[0] == 'a' ? UPDATE_PROMPT_ASK : p[0] == 'w' ? UPDATE_PROMPT_WORKING : p[0] == 'r' ? UPDATE_PROMPT_RESTART : UPDATE_PROMPT_ERROR;
}
void Updater_AutoAnswer(bool yes) { (void)yes; }
static bool sAutoUpdate = true;
bool Port_Config_GetAutoUpdate(void) { return sAutoUpdate; }
void Port_Config_ToggleAutoUpdate(void) { sAutoUpdate = !sAutoUpdate; }
void Updater_SetChannel(bool prerelease) { (void)prerelease; }
unsigned Updater_GetNotes(char* out, unsigned capacity) { if (capacity) out[0] = 0; return 0; }
/* UPDATE_BETA=1: a beta found on the + BETAS channel, to preview the RELEASE cell. */
void Updater_GetStatus(UpdateStatus* out) {
    memset(out, 0, sizeof(*out));
    if (getenv("UPDATE_BETA")) {
        out->state = UPDATE_AVAILABLE;
        out->prerelease = true;
        snprintf(out->version, sizeof(out->version), "v0.1.2");
        snprintf(out->message, sizeof(out->message), "UPDATE AVAILABLE");
    }
}
unsigned Update_FormatNotes(const char* md, char lines[][43], unsigned capacity) {
    (void)md; (void)lines; (void)capacity; return 0;
}

/* ---- game and ROM tables: empty, as before Port_LoadRom ---- */
uint8_t* gRomData = NULL;
uint32_t gRomSize = 0;
#define EMPTY(name) unsigned char name[1 << 16] __attribute__((aligned(16)))
EMPTY(gActiveRegion); EMPTY(gAreaMetadata); EMPTY(gAreaRoomHeaders); EMPTY(gDungeonFloorMetadatas);
EMPTY(gDungeonLayouts); EMPTY(gGfxGroups); EMPTY(gGlobalGfxAndPalettes); EMPTY(gKinstoneWorldEvents);
EMPTY(gKinstoneWorldEvents_eu); EMPTY(gKinstoneWorldEvents_jp); EMPTY(gMapData); EMPTY(gOverworldLocations);
EMPTY(gPaletteGroups); EMPTY(gSpriteAnimations_322); EMPTY(gUnk_08109248); EMPTY(gUnk_0810926C);
EMPTY(gUnk_081092AC); EMPTY(gUnk_0810942E); EMPTY(gUnk_081094CE); EMPTY(gUnk_08128024);
EMPTY(gUnk_08128C14); EMPTY(gUnk_08128C94); EMPTY(gUnk_08128D70); EMPTY(gUnk_08128DE8);
EMPTY(gUnk_08128E94); EMPTY(gUnk_08128F58); EMPTY(gUnk_085C4620); EMPTY(gWorldEvents); EMPTY(gWorldEvents_eu);
void* GetOverworldLocation(void) { return NULL; }
uint32_t GetRoomProperty(void) { return 0; }
void* Port_GetDirectSpriteFrame(void) { return NULL; }
void* Port_GetKinstonePieceTiles(void) { return NULL; }
void* Port_GetRawGfxSpanForVram(void) { return NULL; }
void* Port_GetRawGfxSpanForVramLang(void) { return NULL; }
uint32_t sub_0801DF10(void) { return 0; }
void* Port_GetRawPaletteGroupBankData(void) { return NULL; }
void* Port_GetRawPaletteGroupData(void) { return NULL; }
void* Port_GetRawPaletteGroupEntryData(void) { return NULL; }
void* Port_GetRoomHeaderSafe(void) { return NULL; }
void* Port_GetSpritePtr(void) { return NULL; }
void* Port_GetSpriteSizeTable(void) { return NULL; }
int Port_IsRoomHeaderPtrReadable(void) { return 0; }
int Port_LZ77DecompressToBuffer(void) { return 0; }
uint32_t Port_RemapMapOffset(uint32_t x) { return x; }
uint32_t Port_RemapSpriteIndex(uint32_t x) { return x; }
void* port_resolve_addr(void) { return NULL; }
void* Port_ResolveGfxGroupVram(void) { return NULL; }
void sub_080AD8F0(void) {}
void UnpackTextNibbles(void) {}

/* ---- debug tools (debug_log_3ds.c, debug_perf_3ds.c, port_config_3ds.c) ---- */
static bool sLog = true, sDirect, sPerf;
void DebugLog_Start(void) { sLog = true; }
void DebugLog_Stop(void) { sLog = false; }
bool DebugLog_Running(void) { return sLog; }
void DebugLog_SetDirect(bool d) { sDirect = d; }
bool DebugLog_Direct(void) { return sDirect; }
int DebugLog_FileNumber(void) { return sLog ? 7 : 0; }
void DebugLog_Printf(const char* fmt, ...) { (void)fmt; }
int DebugLog_Mark(void) { return 1; }
bool DebugPerf_Start(void) { sPerf = true; return true; }
int DebugPerf_Stop(void) { sPerf = false; return 3; }
bool DebugPerf_Running(void) { return sPerf; }
unsigned DebugPerf_Rows(void) { return sPerf ? 1834 : 0; }
unsigned DebugPerf_Capacity(void) { return 36000; }
static int sSw[10] = { 1, 0, 0, 0, 1, -1, -1, 0, 0, 0 };
static const char* const kSwNames[10] = { "RENDERER", "FRAME SYNC", "VBLANK LOCK", "STATIC QUAD", "MAP SKIP",
                                          "BOTTOM CORE", "AUDIO CORE", "COMPACT UP", "CPU LIMIT", "STEREO 3D" };
const char* DebugSwitch_Name(int id) { return kSwNames[id]; }
void DebugSwitch_Value(int id, char* out, unsigned cap) {
    if (id == 0) snprintf(out, cap, "%s", sSw[0] ? "GPU" : "CPU (TEST)");
    else if (id == 5 || id == 6) snprintf(out, cap, "%s", sSw[id] < 0 ? "AUTO" : (sSw[id] ? "CORE 1" : "CORE 0"));
    else if (id == 8) snprintf(out, cap, "AUTO 80%%");
    else snprintf(out, cap, "%s", sSw[id] ? "ON" : "OFF");
}
void DebugSwitch_Cycle(int id) { sSw[id] = sSw[id] == 1 ? (id == 5 || id == 6 ? -1 : 0) : 1; }
bool DebugSwitch_IsDefault(int id) { static const int d[10] = { 1, 0, 0, 0, 1, -1, -1, 0, 0, 0 }; return sSw[id] == d[id]; }
void PortPpuGpu3DS_GetStats(PortPpuGpu3DSStats* st) { memset(st, 0, sizeof(*st)); st->initialized = true; st->parityChecks = 2; }
void PortPpuGpu3DS_RequestParityCheck(void) {}
bool FtpServer_Running(void) { return true; }
void FtpServer_Address(char* out, unsigned cap) { snprintf(out, cap, "192.168.1.50:5000"); }
void FtpServer_Stats(unsigned* a, unsigned* b, bool* c) { *a = 3; *b = 1; *c = false; }
bool FtpServer_Start(void) { return true; }
void FtpServer_Stop(void) {}
/* DEVFTP=1 previews the install-only server's bar (production builds with dev-ftp.txt). */
bool FtpServer_InstallOnly(void) { return getenv("DEVFTP") != NULL; }
void FtpServer_Cancel(void) {}

/* ---- save states, FTP and debug switches added since (save_state_3ds.c, ftp_server_3ds.c) ---- */
#include "save_state_3ds.h"
static SaveStateInfo sFakeStates[3] = {
    { .id = 3, .savedAt = 1760000000, .area = 0x03, .room = 1, .health = 72, .maxHealth = 96, .rupees = 120, .compatible = true },
    { .id = 2, .savedAt = 1759900000, .area = 0x22, .room = 0x10, .health = 80, .maxHealth = 96, .rupees = 300, .compatible = true },
    { .id = 1, .savedAt = 1759800000, .area = 0x10, .room = 0, .health = 40, .maxHealth = 64, .rupees = 5, .compatible = false },
};
bool SaveState_Request(SaveStateJob job, uint32_t id) { (void)job; (void)id; return true; }
bool SaveState_Busy(void) { return false; }
SaveStateJob SaveState_PendingJob(void) { return SAVE_STATE_JOB_NONE; }
uint32_t SaveState_Generation(void) { return 0; }
bool SaveState_LastResult(char* m, unsigned cap, uint32_t* id) { (void)cap; if (m) m[0] = 0; if (id) *id = 0; return true; }
bool SaveState_CanSave(void) { return true; }
int SaveState_Count(void) { return 3; }
bool SaveState_InfoAt(int i, SaveStateInfo* out) { if (i < 0 || i >= 3) return false; *out = sFakeStates[i]; return true; }
bool SaveState_ReadThumb(uint32_t id, uint16_t* px) { (void)id; (void)px; return false; }
const char* SaveState_AreaName(unsigned a) { return a == 3 ? "HYRULE FIELD" : a == 0x22 ? "LINKS HOUSE" : NULL; }
const char* FtpServer_Notice(void) { return NULL; }
int FtpServer_Progress(unsigned long long* done, unsigned long long* total) { *done = *total = 0; return 0; }
static bool sDbgLog, sDbgDirect, sDbgFtp;
bool Port_Config_DebugLogOn(void) { return sDbgLog; }
void Port_Config_SetDebugLogOn(bool on) { sDbgLog = on; }
bool Port_Config_DebugLogDirect(void) { return sDbgDirect; }
void Port_Config_SetDebugLogDirect(bool on) { sDbgDirect = on; }
bool Port_Config_DebugFtpOn(void) { return sDbgFtp; }
void Port_Config_SetDebugFtpOn(bool on) { sDbgFtp = on; }

/* ---- X/Y soft slots (port/port_softslots.c): none assigned ---- */
uint8_t Port_SoftSlots_GetAssignment(int slot) { (void)slot; return 0; }
void Port_SoftSlots_SetAssignment(int slot, uint8_t itemId) { (void)slot; (void)itemId; }

/* ---- RetroAchievements (retro_ach.h): a logged-in account and a sample set; RA_OFF=1 shows
 * the tab before logging in, RA_TOAST=1 the unlock notice on the bottom screen. ---- */
#include "retro_ach.h"
static const RaAchievement kRaSample[] = {
    { 1, "Sword of the Smith", "Obtain the Smith's Sword", "", 5, kRaTypeProgression, true, 1760000000 },
    { 2, "Deepwood Shrine", "Defeat the Big Green ChuChu in the Deepwood Shrine", "", 10, kRaTypeProgression, true,
      1760100000 },
    { 3, "Kinstone Collector", "Fuse 50 Kinstones", "", 25, kRaTypeStandard, false, 0 },
    { 4, "Gone Missing", "Talk to the postman before he leaves town for good on his route", "", 5, kRaTypeMissable,
      false, 0 },
    { 5, "Hero of Men", "Defeat Vaati and save Hyrule", "", 50, kRaTypeWin, false, 0 },
    { 6, "Figurine Hunter", "Complete the figurine collection", "", 25, kRaTypeStandard, false, 0 },
};
static bool RaOff(void) { return getenv("RA_OFF") != NULL; }
bool RetroAch_Enabled(void) { return !RaOff(); }
bool RetroAch_LoggedIn(void) { return !RaOff(); }
RaStatus RetroAch_Status(void) { return RaOff() ? kRaNoAccount : kRaOnline; }
void RetroAch_User(char* out, int cap) { snprintf(out, (size_t)cap, "%s", RaOff() ? "" : "tinaut1986"); }
void RetroAch_Message(char* out, int cap) { if (cap > 0) out[0] = 0; }
int RetroAch_Count(void) { return RaOff() ? 0 : (int)(sizeof(kRaSample) / sizeof(kRaSample[0])); }
bool RetroAch_CopyAt(int i, RaAchievement* out) {
    if (i < 0 || i >= RetroAch_Count()) return false;
    *out = kRaSample[i];
    return true;
}
int RetroAch_UnlockedCount(void) { return RaOff() ? 0 : 2; }
uint32_t RetroAch_Points(bool unlocked_only) { return RaOff() ? 0 : unlocked_only ? 15 : 120; }
const uint32_t* RetroAch_Badge(const char* badge, int size) { (void)badge, (void)size; return NULL; }
uint32_t RetroAch_Version(void) { return 1; }
bool RetroAch_Toast(RaAchievement* out) {
    if (!getenv("RA_TOAST")) return false;
    if (out) *out = kRaSample[1];
    return true;
}
bool RetroAch_NotifyTop(void) { return false; }
void RetroAch_SetNotifyTop(bool top) { (void)top; }
bool RetroAch_Sound(void) { return true; }
void RetroAch_SetSound(bool on) { (void)on; }
RaSort RetroAch_Sort(void) { return kRaSortDefault; }
bool RetroAch_Descending(void) { return false; }
void RetroAch_SetSort(RaSort sort, bool descending) { (void)sort, (void)descending; }
void RetroAch_SetEnabled(bool on) { (void)on; }
void RetroAch_PromptLogin(void) {}
void RetroAch_Logout(void) {}
void RetroAch_ShowPreview(void) {}
