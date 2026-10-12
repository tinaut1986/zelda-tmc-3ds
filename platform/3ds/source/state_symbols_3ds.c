#include "state_symbols_3ds.h"

#include <stdlib.h>
#include <string.h>

extern const volatile unsigned char gStateSymbolBlob[STATE_SYMBOL_BLOB_SIZE];

/* 3dsx.ld: the game's data and .bss brackets. */
extern char __ss_data_start[], __ss_data_end[], __ss_bss_start[], __ss_bss_end[];

#define TABLE_MAGIC 0x31545354u /* 'TST1' */

typedef struct SymEntry {
    uint32_t hash;
    uint32_t address;
    uint32_t size;
} SymEntry;

static StateVar* sVars;
static int sVarCount;
static SymEntry* sByAddress; /* in the blob's order */
static SymEntry* sByHash;
static int sSymCount;
static bool sParsed;

static int ByAddress(const void* a, const void* b) {
    const uintptr_t x = (uintptr_t)((const StateVar*)a)->ptr, y = (uintptr_t)((const StateVar*)b)->ptr;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int ByHash(const void* a, const void* b) {
    const uint32_t x = ((const SymEntry*)a)->hash, y = ((const SymEntry*)b)->hash;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void Parse(void) {
    if (sParsed) return;
    sParsed = true;
    uint32_t head[4];
    for (int i = 0; i < 16; ++i) ((unsigned char*)head)[i] = gStateSymbolBlob[i];
    if (head[0] != TABLE_MAGIC) return;
    const uint32_t nVars = head[1], nSyms = head[2], nameBytes = head[3];
    if (16u + nVars * 12u + nSyms * 12u + nameBytes > STATE_SYMBOL_BLOB_SIZE) return;
    unsigned char* blob = malloc(STATE_SYMBOL_BLOB_SIZE);
    if (!blob) return;
    for (uint32_t i = 0; i < 16u + nVars * 12u + nSyms * 12u + nameBytes; ++i) blob[i] = gStateSymbolBlob[i];
    const unsigned char* vtab = blob + 16;
    const unsigned char* stab = vtab + nVars * 12u;
    char* names = (char*)(stab + nSyms * 12u);

    sVars = malloc(nVars * sizeof(StateVar));
    sByAddress = malloc(nSyms * sizeof(SymEntry));
    sByHash = malloc(nSyms * sizeof(SymEntry));
    if (!sVars || !sByAddress || !sByHash) {
        free(blob);
        return;
    }
    const uintptr_t dataSize = (uintptr_t)(__ss_data_end - __ss_data_start);
    const uintptr_t bssSize = (uintptr_t)(__ss_bss_end - __ss_bss_start);
    int n = 0;
    for (uint32_t i = 0; i < nVars; ++i) {
        uint32_t nameOff, regionOff, size;
        memcpy(&nameOff, vtab + i * 12u, 4);
        memcpy(&regionOff, vtab + i * 12u + 4, 4);
        memcpy(&size, vtab + i * 12u + 8, 4);
        const bool bss = (regionOff >> 31) != 0;
        const uint32_t off = regionOff & 0x7FFFFFFFu;
        if (nameOff >= nameBytes || off + size > (bss ? bssSize : dataSize)) continue; /* another binary's table */
        sVars[n].name = names + nameOff;
        sVars[n].ptr = (bss ? __ss_bss_start : __ss_data_start) + off;
        sVars[n].size = size;
        ++n;
    }
    qsort(sVars, (size_t)n, sizeof(StateVar), ByAddress);
    sVarCount = n;
    /* names stay in `blob`, which is never freed */
    memcpy(sByAddress, stab, nSyms * sizeof(SymEntry));
    memcpy(sByHash, stab, nSyms * sizeof(SymEntry));
    qsort(sByHash, nSyms, sizeof(SymEntry), ByHash);
    sSymCount = (int)nSyms;
}

bool StateSymbols_Ready(void) {
    Parse();
    return sVarCount > 0 && sSymCount > 0;
}

int StateVars_Count(void) {
    Parse();
    return sVarCount;
}

const StateVar* StateVars_At(int index) {
    Parse();
    return index >= 0 && index < sVarCount ? &sVars[index] : NULL;
}

const StateVar* StateVars_Find(const char* name) {
    Parse();
    for (int i = 0; i < sVarCount; ++i)
        if (!strcmp(sVars[i].name, name)) return &sVars[i];
    return NULL;
}

const StateVar* StateVars_Containing(uintptr_t p, uint32_t* offset) {
    Parse();
    int lo = 0, hi = sVarCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uintptr_t base = (uintptr_t)sVars[mid].ptr;
        if (p < base) hi = mid - 1;
        else if (p >= base + sVars[mid].size) lo = mid + 1;
        else {
            if (offset) *offset = (uint32_t)(p - base);
            return &sVars[mid];
        }
    }
    return NULL;
}

uint32_t StateSymbols_HashAt(uintptr_t address) {
    Parse();
    int lo = 0, hi = sSymCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (address < sByAddress[mid].address) hi = mid - 1;
        else if (address > sByAddress[mid].address) lo = mid + 1;
        else return sByAddress[mid].hash;
    }
    return 0;
}

uint32_t StateSymbols_HashContaining(uintptr_t address, uint32_t* offset) {
    Parse();
    int lo = 0, hi = sSymCount - 1, best = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (sByAddress[mid].address <= address) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (best < 0 || sByAddress[best].size == 0 || address >= (uintptr_t)sByAddress[best].address + sByAddress[best].size)
        return 0;
    if (offset) *offset = (uint32_t)(address - sByAddress[best].address);
    return sByAddress[best].hash;
}

uintptr_t StateSymbols_AddressOf(uint32_t hash) {
    Parse();
    int lo = 0, hi = sSymCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (hash < sByHash[mid].hash) hi = mid - 1;
        else if (hash > sByHash[mid].hash) lo = mid + 1;
        else return sByHash[mid].address;
    }
    return 0;
}
