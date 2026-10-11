#include "net_3ds.h"

#include <3ds.h>
#include <malloc.h>
#include <stdlib.h>

enum { SOC_BUFFER_SIZE = 1024 * 1024 };

static LightLock sLock = 1; /* LightLock's unlocked value */
static int sRefs;
static void* sSocBuffer;
static bool sAcReady;

bool Net3DS_Acquire(void) {
    LightLock_Lock(&sLock);
    bool ok = true;
    if (sRefs == 0) {
        sAcReady = R_SUCCEEDED(acInit());
        sSocBuffer = memalign(0x1000, SOC_BUFFER_SIZE);
        if (!sAcReady || !sSocBuffer || R_FAILED(socInit(sSocBuffer, SOC_BUFFER_SIZE))) {
            free(sSocBuffer);
            sSocBuffer = NULL;
            if (sAcReady) acExit();
            sAcReady = false;
            ok = false;
        }
    }
    if (ok) ++sRefs;
    LightLock_Unlock(&sLock);
    return ok;
}

void Net3DS_Release(void) {
    LightLock_Lock(&sLock);
    if (sRefs > 0 && --sRefs == 0) {
        socExit();
        free(sSocBuffer);
        sSocBuffer = NULL;
        if (sAcReady) acExit();
        sAcReady = false;
    }
    LightLock_Unlock(&sLock);
}

bool Net3DS_WifiConnected(void) {
    u32 wifi = 0;
    return sAcReady && R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi != 0;
}
