#include "cia_install_3ds.h"

#include "app_paths_3ds.h"
#include "debug_log_3ds.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

enum { CIA_IO_SIZE = 128 * 1024 };

/* Written by the installing (server) thread, read by the main one (CiaInstall_Progress). */
static volatile unsigned long long sProgressDone, sProgressTotal;

void CiaInstall_Progress(unsigned long long* done, unsigned long long* total) {
    *done = sProgressDone;
    *total = sProgressTotal;
}

static bool EnoughSpace(u64 required) {
    FS_ArchiveResource r;
    return R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r)) && (u64)r.freeClusters * r.clusterSize >= required;
}

bool CiaInstall_File(const char* sdPath, char* error, unsigned errorCap) {
    Result rc = amInit();
    if (R_FAILED(rc)) {
        snprintf(error, errorCap, "AM unavailable (%08lx)", (unsigned long)rc);
        return false;
    }
    Handle input = 0, output = 0;
    bool started = false, ok = false, downgraded = false;
    u64 size = 0, required = 0;
    AM_TitleInfo info;
    unsigned char* data = malloc(CIA_IO_SIZE);
    snprintf(error, errorCap, "out of memory");
    if (!data) goto done;
    rc = FSUSER_OpenFileDirectly(&input, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, sdPath),
                                 FS_OPEN_READ, 0);
    snprintf(error, errorCap, "cannot open %s", sdPath);
    if (R_FAILED(rc) || R_FAILED(FSFILE_GetSize(input, &size))) goto done;
    rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, input);
    snprintf(error, errorCap, "not a CIA of this game (title %016llx)", R_SUCCEEDED(rc) ? info.titleID : 0ull);
    if (R_FAILED(rc) || info.titleID != TMC3DS_TITLE_ID) goto done;
    rc = AM_GetCiaRequiredSpace(&required, MEDIATYPE_SD, input);
    snprintf(error, errorCap, "not enough SD space");
    if (R_FAILED(rc) || !EnoughSpace(required + 4 * 1024 * 1024ull)) goto done;
    /* Overwrite through AM; the installed title is only removed if AM refuses a lower version (below). */
    rc = AM_StartCiaInstallOverwrite(&output, MEDIATYPE_SD);
    snprintf(error, errorCap, "install start failed (%08lx)", (unsigned long)rc);
    if (R_FAILED(rc)) goto done;
    started = true;
    sProgressDone = 0, sProgressTotal = size;
    for (u64 offset = 0; offset < size;) {
        u32 read = 0, wrote = 0, want = size - offset > CIA_IO_SIZE ? CIA_IO_SIZE : (u32)(size - offset);
        rc = FSFILE_Read(input, &read, offset, data, want);
        snprintf(error, errorCap, "SD read failed (%08lx)", (unsigned long)rc);
        if (R_FAILED(rc) || read != want) goto done;
        rc = FSFILE_Write(output, &wrote, offset, data, read, 0);
        if (R_FAILED(rc) && offset == 0 && !downgraded) {
            /* AM refuses a CIA whose title version is lower than the installed one (0xd8e08027) at the
             * first write. This is the developer path (FTP SITE INSTALL), so remove the installed title
             * and install this one instead: any version can be installed, downgrades included. Saves
             * and settings live under sdmc:/3ds/, not in the title, so they stay. */
            DebugLog_Printf("cia install: write refused (%08lx), reinstalling over a removed title", (unsigned long)rc);
            AM_CancelCIAInstall(output);
            started = false;
            downgraded = true;
            rc = AM_DeleteTitle(MEDIATYPE_SD, TMC3DS_TITLE_ID);
            snprintf(error, errorCap, "removing the installed title failed (%08lx)", (unsigned long)rc);
            if (R_FAILED(rc)) goto done;
            rc = AM_StartCiaInstall(MEDIATYPE_SD, &output);
            snprintf(error, errorCap, "install restart failed (%08lx)", (unsigned long)rc);
            if (R_FAILED(rc)) goto done;
            started = true;
            continue;
        }
        snprintf(error, errorCap, "install write failed (%08lx)", (unsigned long)rc);
        if (R_FAILED(rc) || wrote != read) goto done;
        offset += read;
        sProgressDone = offset;
    }
    rc = AM_FinishCiaInstall(output);
    started = false;
    ok = R_SUCCEEDED(rc);
    snprintf(error, errorCap, ok ? "" : "install finish failed (%08lx)", (unsigned long)rc);
done:
    if (started) AM_CancelCIAInstall(output);
    if (input) FSFILE_Close(input);
    free(data);
    amExit();
    DebugLog_Printf("cia install %s: %s%s", sdPath, ok ? "installed" : "failed: ", ok ? "" : error);
    return ok;
}
