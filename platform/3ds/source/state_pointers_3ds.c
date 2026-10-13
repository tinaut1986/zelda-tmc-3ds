/* Host pointers in a save state. See docs/save-states.md.
 *
 * The game keeps pointers to its own code, constants and other globals. They
 * move with every rebuild, so a state writes them as relocations. Telling a
 * pointer from a number that happens to look like one (a tile pair, a fixed
 * point position) needs rules, applied to the game's globals by name:
 *
 *   1. A word that is exactly the start of a function or constant of the
 *      game, or of another saved global, is a pointer. Numbers do not land on
 *      such an address except by a one-in-65536 coincidence.
 *   2. A word that points inside another saved global is a pointer only where
 *      the holder is known to keep such pointers (kInner, and the entity
 *      arrays at the offsets of Entity's links).
 *   3. Globals bigger than kScanLimit (tile maps, the emulated memories) are
 *      saved but never scanned: the words of a tile map are all candidates.
 *   4. kNotSaved: the audio engine's bookkeeping, restarted from the music id.
 *
 * Pointers into the ROM image are not handled here: the image is allocated at
 * boot, and the loader moves every word that points into it
 * (save_state_3ds.c). */
#include "state_pointers_3ds.h"

#include "state_symbols_3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kScanLimit = 16384, ENTITY_SIZE = 0x88 };

static const char* const kNotSaved[] = {
    "gMPlayTracks", "gMPlayInfos", "gMPlayInfos2", "gMPlayMemAccArea",
};

/* Holders of pointers into the middle of other globals. */
static const char* const kInner[] = {
    "gAreaTileSets", "gAreaRoomMaps", "gAreaTable", "gEntityLists", "gUnk_02033290", "gUnk_03003C70",
    "gCollidableList", "gPossibleInteraction", "gEntityScriptCtxTable", "gTextRender", "gUnk_08107BE0",
    "gTextVariableSources", "gScreen", "gArea", "gRoomControls", "gUnk_020000C0", "gUnk_02018EA0",
    "gCurrentRoomMemory",
};

/* Globals the game keeps pointers into the middle of from many places (the BG settings of
 * gScreen: gUI saves them, the map layers hold them): a word pointing into one of these is a
 * pointer whatever global holds it. */
static const char* const kInnerTargets[] = {
    "gScreen",
    "gScriptExecutionContextArray", /* every entity's scriptContext field points into it */
};

/* Globals too big to scan whose first word is a pointer (the map layers' BG settings). */
static const char* const kHeadOnly[] = {
    "gMapBottom", "gMapTop",
};

/* Tables made only of pointers into the middle of the game's constants. */
static const char* const kConstInner[] = {
    "gBgAnimations",
};

/* Offsets in an entity slot that hold links to other entities: prev, next,
 * contactedEntity, parent, child. The subclass fields (0x68..) hold an entity
 * only when it is exactly a slot. */
static bool EntityLinkOffset(uint32_t offsetInSlot) {
    return offsetInSlot == 0x00 || offsetInSlot == 0x04 || offsetInSlot == 0x4c || offsetInSlot == 0x50 ||
           offsetInSlot == 0x54;
}

static bool InList(const char* const* list, size_t n, const char* name) {
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(list[i], name)) return true;
    return false;
}

bool StatePointers_Excluded(const char* var) {
    return InList(kNotSaved, sizeof(kNotSaved) / sizeof(kNotSaved[0]), var);
}

static bool IsEntityArray(const char* name) {
    return !strcmp(name, "gEntities") || !strcmp(name, "gPlayerEntity");
}

static int Append(StateReloc** arr, int* n, int* cap, const StateReloc* r) {
    if (*n == *cap) {
        const int grown = *cap ? *cap * 2 : 256;
        StateReloc* p = realloc(*arr, (size_t)grown * sizeof(StateReloc));
        if (!p) return 0;
        *arr = p;
        *cap = grown;
    }
    (*arr)[(*n)++] = *r;
    return 1;
}

int StatePointers_Collect(StateReloc** out) {
    *out = NULL;
    StateReloc* arr = NULL;
    int n = 0, cap = 0;
    const int count = StateVars_Count();
    for (int i = 0; i < count; ++i) {
        const StateVar* v = StateVars_At(i);
        const bool headOnly = v->size > kScanLimit && InList(kHeadOnly, sizeof(kHeadOnly) / sizeof(kHeadOnly[0]), v->name);
        if (StatePointers_Excluded(v->name) || v->size < 4 || (v->size > kScanLimit && !headOnly) ||
            ((uintptr_t)v->ptr & 3u))
            continue;
        const bool inner = InList(kInner, sizeof(kInner) / sizeof(kInner[0]), v->name);
        const bool entities = IsEntityArray(v->name);
        const bool constInner = InList(kConstInner, sizeof(kConstInner) / sizeof(kConstInner[0]), v->name);
        const uint32_t* w = (const uint32_t*)v->ptr;
        const uint32_t words = headOnly ? 1u : v->size / 4u;
        for (uint32_t k = 0; k < words; ++k) {
            const uint32_t value = w[k];
            if (value < 0x100000u) continue;
            StateReloc r;
            memset(&r, 0, sizeof(r));
            const uint32_t wordOff = k * 4u;

            uint32_t hash = StateSymbols_HashAt(value);
            uint32_t symOff = 0;
            /* An entity's hitbox points into the game's table of hitboxes. */
            if (!hash && entities && wordOff % ENTITY_SIZE == 0x48) hash = StateSymbols_HashContaining(value, &symOff);
            uint32_t off = 0;
            const StateVar* t = hash ? NULL : StateVars_Containing(value, &off);
            if (!hash && t) {
                bool allowed = off == 0 || inner || InList(kInnerTargets, sizeof(kInnerTargets) / sizeof(kInnerTargets[0]), t->name);
                if (entities && !allowed) {
                    const uint32_t slot = wordOff % ENTITY_SIZE;
                    allowed = EntityLinkOffset(slot) || (slot >= 0x68 && IsEntityArray(t->name) &&
                                                         off % ENTITY_SIZE == 0);
                }
                if (!allowed) continue;
                r.kind = STATE_RELOC_VAR;
                snprintf(r.target, sizeof(r.target), "%s", t->name);
                r.targetOff = off;
            } else {
                /* A holder of pointers into the middle of the game's constant
                 * tables (gBgAnimations' frame lists). */
                if (!hash && constInner) hash = StateSymbols_HashContaining(value, &symOff);
                if (!hash) continue;
                r.kind = STATE_RELOC_FUNC;
                snprintf(r.target, sizeof(r.target), "%08x", (unsigned)hash);
                r.targetOff = symOff;
            }
            snprintf(r.holder, sizeof(r.holder), "%s", v->name);
            r.wordOff = wordOff;
            if (!Append(&arr, &n, &cap, &r)) {
                *out = arr;
                return n;
            }
        }
    }
    *out = arr;
    return n;
}

bool StatePointers_Apply(const StateReloc* r) {
    const StateVar* holder = StateVars_Find(r->holder);
    if (!holder || r->wordOff + 4u > holder->size) return false;
    uint32_t value = 0;
    bool ok = false;
    if (r->kind == STATE_RELOC_FUNC) {
        unsigned hash = 0;
        const uintptr_t a = sscanf(r->target, "%x", &hash) == 1 ? StateSymbols_AddressOf(hash) : 0;
        if (a) {
            value = (uint32_t)(a + r->targetOff);
            ok = true;
        }
    } else if (r->kind == STATE_RELOC_VAR) {
        const StateVar* t = StateVars_Find(r->target);
        if (t && r->targetOff <= t->size) {
            value = (uint32_t)((uintptr_t)t->ptr + r->targetOff);
            ok = true;
        }
    }
    *(uint32_t*)((char*)holder->ptr + r->wordOff) = ok ? value : 0u;
    return ok;
}
