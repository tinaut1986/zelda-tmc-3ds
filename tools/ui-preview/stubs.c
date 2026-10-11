/* Host stand-ins for what the bottom-screen compositor reaches outside
 * port_second_screen*.c: the 3DS platform, the runtime config, the updater and
 * the game/ROM tables. With no ROM loaded every ROM lookup returns nothing, so
 * the theme and the map/item painters take their own fallbacks; the settings
 * and debug pages draw exactly as on the console. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
void Port_PPU_3DS_WriteQuickDump(void) {}
void Port_PPU_SetColorCorrection(bool on) { (void)on; }
PortDumpStateResult Port_DumpState3DS_LoadLatest(void) { return (PortDumpStateResult)0; }
const char* Port_DumpState_ResultLabel(PortDumpStateResult r) { (void)r; return "OK"; }
void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) { (void)itemId; (void)slot; }

/* ---- runtime config: plain variables ---- */
static int sUiTheme = 0;
int Port_Config_GetUiTheme(void) { return sUiTheme; }
void Port_Config_SetUiTheme(int theme) { sUiTheme = theme; }
static int sAspect = 0, sStyle = 2, sBackdrop = 0;
static bool sColor = true, sHideHud = false, sHold = true, sCrests = true, sFloor = true, sFollow = true;
static bool sSwap = false, sFps = false, sWide = true, sAuto = true;
static unsigned sTurbo = 5;
static float sVolume = 1.0f;
static const char* const kAspect[] = { "WIDE", "ORIGINAL", "STRETCH" };
static const char* const kStyle[] = { "BLUR", "BILINEAR", "PIXEL PERFECT" };
void Port_Config_Cycle3DSAspectRatio(void) { sAspect = (sAspect + 1) % 3; }
void Port_Config_Cycle3DSDisplayStyle(void) { sStyle = (sStyle + 1) % 3; }
const char* Port_Config_Get3DSAspectRatioName(void) { return kAspect[sAspect]; }
int Port_Config_Get3DSDisplayStyle(void) { return sStyle; }
const char* Port_Config_Get3DSDisplayStyleName(void) { return kStyle[sStyle]; }
bool Port_Config_GetColorCorrection(void) { return sColor; }
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
void Updater_SetChannel(bool prerelease) { (void)prerelease; }
unsigned Updater_GetNotes(char* out, unsigned capacity) { if (capacity) out[0] = 0; return 0; }
void Updater_GetStatus(UpdateStatus* out) { memset(out, 0, sizeof(*out)); }
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
static int sSw[11] = { 1, 0, 0, 0, 1, 0, -1, -1, 0, 0, 0 };
static const char* const kSwNames[11] = { "RENDERER", "FRAME SYNC", "VBLANK LOCK", "STATIC QUAD", "MAP SKIP",
                                          "FRAME LOG", "BOTTOM CORE", "AUDIO CORE", "COMPACT UP", "BOTTOM 565",
                                          "CPU LIMIT" };
const char* DebugSwitch_Name(int id) { return kSwNames[id]; }
void DebugSwitch_Value(int id, char* out, unsigned cap) {
    if (id == 0) snprintf(out, cap, "%s", sSw[0] ? "GPU" : "CPU");
    else if (id == 6 || id == 7) snprintf(out, cap, "%s", sSw[id] < 0 ? "AUTO" : (sSw[id] ? "CORE 1" : "CORE 0"));
    else if (id == 10) snprintf(out, cap, "AUTO");
    else snprintf(out, cap, "%s", sSw[id] ? "ON" : "OFF");
}
void DebugSwitch_Cycle(int id) { sSw[id] = sSw[id] == 1 ? (id == 6 || id == 7 ? -1 : 0) : 1; }
bool DebugSwitch_AtBoot(int id) { return id >= 6; }
bool DebugSwitch_Pending(int id) { return id == 7 && sSw[7] != -1; }
bool DebugSwitch_IsDefault(int id) { static const int d[11] = { 1, 0, 0, 0, 1, 0, -1, -1, 0, 0, 0 }; return sSw[id] == d[id]; }
void PortPpuGpu3DS_GetStats(PortPpuGpu3DSStats* st) { memset(st, 0, sizeof(*st)); st->initialized = true; st->parityChecks = 2; }
void PortPpuGpu3DS_RequestParityCheck(void) {}
