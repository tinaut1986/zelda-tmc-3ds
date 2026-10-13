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
#include "upstream_import_3ds.h"
#include "boot_text_3ds.h"
#include "tmc3ds_build_config.h"
#if TMC3DS_DEBUG_TOOLS
#include "debug_log_3ds.h"
#include "debug_perf_3ds.h"
#include "debug_switches_3ds.h"
#endif
#include "ftp_server_3ds.h"

#define APP_DIR TMC3DS_APP_DIR
#define ROM_PATH_SIZE 512
#define ROM_SHA1_USA "b4bd50e4131b027c334547b4524e2dbbd4227130"
#define ROM_SHA1_EUROPE "cff199b36ff173fb6faf152653d1bccf87c26fb7"

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

/* The first supported .gba of `dirPath` into `out`, prefixed with `prefix`
 * ("" for the working directory). 1 found, -1 only unsupported .gba files, 0 none. */
static int FindRomIn(const char* dirPath, const char* prefix, char* out, size_t outSize) {
    DIR* dir = opendir(dirPath);
    if (!dir) return 0;

    int foundGba = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!HasGbaExtension(entry->d_name)) continue;

        char path[ROM_PATH_SIZE];
        snprintf(path, sizeof(path), "%s%s", prefix, entry->d_name);
        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        foundGba = 1;
        if (!RomIsSupported(path)) continue;
        snprintf(out, outSize, "%s", path);
        closedir(dir);
        return 1;
    }

    closedir(dir);
    return foundGba ? -1 : 0;
}

/* Our folder first; without a ROM there, the upstream port's folder, read in place
 * so the ROM is not on the SD twice. */
static int FindRom(char* out, size_t outSize) {
    const int own = FindRomIn(".", "", out, outSize);
    if (own > 0) return own;
    const int upstream = FindRomIn(UPSTREAM_APP_DIR, UPSTREAM_APP_DIR "/", out, outSize);
    return upstream != 0 ? upstream : own;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!Platform3DS_Init()) return 1;
    Platform3DS_SilenceBootConsole();
    Platform3DS_ShowSplash();

    Platform3DS_BootAnimStart();
    if (!PrepareStorage()) {
        Platform3DS_BootAnimStop();
        char message[256];
        snprintf(message, sizeof(message), "%s\n%s", BootText_Tr("COULD NOT OPEN THIS FOLDER:"), APP_DIR);
        Platform3DS_ShowFatal("STORAGE ERROR", message);
        Platform3DS_Shutdown();
        return 1;
    }

    /* Before the game starts: it reads its saves once, at boot. */
    if (UpstreamImport_ShouldOffer()) {
        Platform3DS_BootAnimStop();
        UpstreamImport_OfferSaves();
        Platform3DS_BootAnimStart();
    }

    char romPath[ROM_PATH_SIZE];
    int romResult = FindRom(romPath, sizeof(romPath));
    if (romResult <= 0) {
        /* In the console's language (port_text_i18n.c); paths and hashes as they are. */
        char message[768];
        if (romResult < 0) {
            snprintf(message, sizeof(message), "%s\n\n%s\n%s\n%s\n\n%s\nUSA: %s\n%s %s",
                     BootText_Tr("NONE OF THE .GBA FILES IS A SUPPORTED USA (BZME) OR EUROPE (BZMP) ROM."),
                     BootText_Tr("FOLDERS SEARCHED:"), APP_DIR, UPSTREAM_APP_DIR, BootText_Tr("EXPECTED SHA-1:"),
                     ROM_SHA1_USA, BootText_Tr("EUROPE:"), ROM_SHA1_EUROPE);
        } else {
            snprintf(message, sizeof(message), "%s\n%s\n%s\n%s\n\n%s\n\n%s\nUSA: %s\n%s %s",
                     BootText_Tr("COPY YOUR CLEAN USA OR EUROPE ROM TO:"), APP_DIR,
                     BootText_Tr("THE ORIGINAL PORT'S FOLDER WORKS TOO:"), UPSTREAM_APP_DIR,
                     BootText_Tr("ANY .GBA FILENAME IS ACCEPTED."), BootText_Tr("EXPECTED SHA-1:"), ROM_SHA1_USA,
                     BootText_Tr("EUROPE:"), ROM_SHA1_EUROPE);
        }
        Platform3DS_BootAnimStop();
        Platform3DS_ShowFatal(romResult < 0 ? "UNSUPPORTED ROM" : "ROM NOT FOUND", message);
        Platform3DS_Shutdown();
        return 1;
    }

    FILE* rom = fopen(romPath, "rb");
    if (!rom) {
        Platform3DS_BootAnimStop();
        Platform3DS_ShowFatal("ROM ERROR", BootText_Tr("COULD NOT OPEN THE SELECTED .GBA FILE."));
        Platform3DS_Shutdown();
        return 1;
    }
    fclose(rom);

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
    Platform3DS_BootAnimStop();
    Port_PPU_Init(NULL);
    if (!Port_Audio_Init()) {
#if TMC3DS_DEBUG_TOOLS
        DebugLog_Printf("audio is unavailable");
#endif
    }

    Platform3DS_EnterGameplayDisplay();
    Updater_Init(argc > 0 ? argv[0] : NULL);
    AgbMain();
#if TMC3DS_DEBUG_TOOLS
    /* Each step is flushed: if one never returns, the log says which. */
#define EXIT_STEP(name)                         \
    do {                                        \
        DebugLog_Printf("exit: %s", name);      \
        DebugLog_Flush();                       \
    } while (0)
    EXIT_STEP("engine returned");
    if (DebugPerf_Running()) DebugPerf_Stop();
#else
#define EXIT_STEP(name) ((void)0)
#endif
    /* Production builds too: the install-only server (dev-ftp.txt). */
    if (FtpServer_Running()) FtpServer_Stop();
    EXIT_STEP("ftp stopped");
    Updater_Shutdown();
    EXIT_STEP("updater stopped");
    Port_PPU_Shutdown();
    EXIT_STEP("ppu stopped");
#if TMC3DS_DEBUG_TOOLS
    DebugLog_Stop();
#endif
    Platform3DS_Shutdown();
    return 0;
}
