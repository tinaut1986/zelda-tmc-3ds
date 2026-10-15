/* Where the RetroAchievements address space lives in this build: the
 * original game's globals by their GBA address (tools/gen_ra_map.py). */
#pragma once

typedef struct {
    unsigned address;      /* RA address: IWRAM 0x00000-0x07FFF, EWRAM 0x08000-0x47FFF */
    unsigned length;       /* bytes mapped here; entries never overlap */
    unsigned char* storage; /* where those bytes live in this build */
} RaMapEntry;

/* Sorted by address. */
extern const RaMapEntry gRaMap[];
extern const unsigned gRaMapCount;
