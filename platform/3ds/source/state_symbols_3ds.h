#pragma once
/* The names behind save states that survive a rebuild (PLAN P1.8b,
 * docs/save-states.md). The table is not C: tools/gen_state_symbols.py builds
 * it from the linked ELF and patches it into the array state_blob_3ds.c
 * reserves, so a binary that was not post-processed has no table and saves
 * the raw format (state vars and symbols report empty). */
#include <stdbool.h>
#include <stdint.h>

#define STATE_SYMBOL_BLOB_SIZE 0x30000

typedef struct StateVar {
    const char* name;
    void* ptr;
    uint32_t size;
} StateVar;

bool StateSymbols_Ready(void);

/* The game's mutable globals, sorted by address. */
int StateVars_Count(void);
const StateVar* StateVars_At(int index);
const StateVar* StateVars_Find(const char* name);
/* The variable holding address p, and p's offset in it; NULL if none. */
const StateVar* StateVars_Containing(uintptr_t p, uint32_t* offset);

/* Functions and constants of the game, by the hash of their name. */
uint32_t StateSymbols_HashAt(uintptr_t address); /* 0 unless address is exactly the start of one */
uint32_t StateSymbols_HashContaining(uintptr_t address, uint32_t* offset); /* inside one (sized ones only) */
uintptr_t StateSymbols_AddressOf(uint32_t hash); /* 0 when this build has none */
