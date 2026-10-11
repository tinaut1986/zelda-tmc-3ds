#include "platform_3ds.h"
#include "tmc3ds_version.h"
#include "updater.h"
#include "port_second_screen_theme.h"

#include "port_audio.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_runtime_config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_paths_3ds.h"
#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#include "debug_perf_3ds.h"
#include "ftp_server_3ds.h"
#include "debug_switches_3ds.h"
#endif

#define APP_DIR TMC3DS_APP_DIR
#define ROM_PATH_SIZE 512

extern void AgbMain(void);

static int PrepareStorage(void) {
    mkdir("sdmc:/3ds", 0777);
    if (mkdir(APP_DIR, 0777) != 0 && errno != EEXIST) return 0;
    return chdir(APP_DIR) == 0;
}

static int HasGbaExtension(const char* name) {
    const size_t length = strlen(name);
    if (length < 5) return 0;
    const char* ext = name + length - 4;
    return tolower((unsigned char)ext[0]) == '.' &&
           tolower((unsigned char)ext[1]) == 'g' &&
           tolower((unsigned char)ext[2]) == 'b' &&
           tolower((unsigned char)ext[3]) == 'a';
}

static int RomIsSupported(const char* path) {
    char gameCode[4];
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fseek(file, 0xAC, SEEK_SET) == 0 && fread(gameCode, 1, sizeof(gameCode), file) == sizeof(gameCode) &&
             (memcmp(gameCode, "BZME", sizeof(gameCode)) == 0 || memcmp(gameCode, "BZMP", sizeof(gameCode)) == 0);
    fclose(file);
    return ok;
}

static int FindRom(char* out, size_t outSize) {
    DIR* dir = opendir(".");
    if (!dir) return 0;

    int foundGba = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!HasGbaExtension(entry->d_name)) continue;

        struct stat info;
        if (stat(entry->d_name, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        foundGba = 1;
        if (!RomIsSupported(entry->d_name)) continue;
        snprintf(out, outSize, "%s", entry->d_name);
        closedir(dir);
        return 1;
    }

    closedir(dir);
    return foundGba ? -1 : 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!Platform3DS_Init()) return 1;
    Platform3DS_ShowSplash();

    printf("Minish Cap 3DS v" TMC_PORT_VERSION_LABEL " (upstream " TMC_UPSTREAM_VERSION ")\n\n");
    printf("agbplay: ipatix and contributors (LGPLv3).\nLicenses: romfs:/licenses/\n\n");
    printf("System: %s\n", Platform3DS_IsNew3DS() ? "New Nintendo 3DS" : "Nintendo 3DS");
    printf("Performance: %s\n",
           Platform3DS_IsNew3DS() ? "New 3DS full presentation"
                                  : "Old 3DS adaptive presentation skip (max 3)");
    printf("PPU worker core 1: %u%%\n", Platform3DS_Core1TimeLimit());
    printf("Extra New 3DS core: %s\n\n", Platform3DS_IsNew3DS() ? "enabled" : "unavailable");
    printf("Preparing storage...\n");
    if (!PrepareStorage()) {
        Platform3DS_ShowFatal("Storage error", "Could not open " APP_DIR ".");
        Platform3DS_Shutdown();
        return 1;
    }

    char romPath[ROM_PATH_SIZE];
    int romResult = FindRom(romPath, sizeof(romPath));
    if (romResult <= 0) {
        char message[512];
        if (romResult < 0) {
            snprintf(message, sizeof(message),
                     "None of the .gba files in:\n%s\n"
                     "is a supported USA (BZME) or Europe (BZMP) ROM.\n\n"
                     "Expected SHA-1:\nUSA: b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                     "Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7",
                     APP_DIR);
        } else {
            snprintf(message, sizeof(message),
                     "Copy your clean USA or Europe ROM to:\n%s\n\nAny .gba filename is accepted.\n\n"
                     "Expected SHA-1:\nUSA: b4bd50e4131b027c334547b4524e2dbbd4227130\n"
                     "Europe: cff199b36ff173fb6faf152653d1bccf87c26fb7",
                     APP_DIR);
        }
        Platform3DS_ShowFatal(romResult < 0 ? "Unsupported ROM" : "ROM not found", message);
        Platform3DS_Shutdown();
        return 1;
    }

    FILE* rom = fopen(romPath, "rb");
    if (!rom) {
        Platform3DS_ShowFatal("ROM error", "Could not open the selected .gba file.");
        Platform3DS_Shutdown();
        return 1;
    }
    fclose(rom);

    printf("Loading ROM and tables...\n");
    Port_Config_Load("tmc3ds.ini");
#if TMC3DS_DEBUG_TOOLS
    /* Debug builds log from boot unless the DEBUG tab left it off; the FTP
     * server comes back on its own once Wi-Fi is up (port_ppu_3ds.c). */
    if (Port_Config_DebugLogOn()) {
        DebugLog_SetDirect(Port_Config_DebugLogDirect());
        DebugLog_Start();
    }
#endif
    Port_LoadRom(romPath);
    Port_SecondScreenTheme_Ready();
    Port_PPU_Init(NULL);
    if (!Port_Audio_Init()) {
        printf("Warning: audio is unavailable.\n");
    }

    printf("Starting engine...\n");
    Platform3DS_EnterGameplayDisplay();
    Updater_Init(argc > 0 ? argv[0] : NULL);
    AgbMain();
#if TMC3DS_DEBUG_TOOLS
    if (FtpServer_Running()) FtpServer_Stop();
    if (DebugPerf_Running()) DebugPerf_Stop();
    DebugLog_Printf("exit: engine returned");
    DebugLog_Stop();
#endif
    Updater_Shutdown();

    Port_PPU_Shutdown();
    Platform3DS_Shutdown();
    return 0;
}
