#pragma once
/* What a save state does with the host pointers in the game's globals
 * (PLAN P1.8b, docs/save-states.md). A pointer is written down in terms that
 * survive a rebuild: a function or constant of the game by the hash of its
 * name, or a place inside another saved global by its name and offset. */
#include <stdbool.h>
#include <stdint.h>

typedef enum StateRelocKind {
    STATE_RELOC_VAR = 1,  /* inside another saved global: target name + offset */
    STATE_RELOC_FUNC = 2, /* a function or constant of the game: target hash */
} StateRelocKind;

typedef struct StateReloc {
    uint8_t kind;
    char holder[64];  /* the global that holds the pointer */
    uint32_t wordOff; /* byte offset of the pointer in it */
    char target[64];  /* VAR: the global's name; FUNC: the name's hash in hex */
    uint32_t targetOff; /* offset inside the target */
} StateReloc;

/* True for globals a state leaves alone: the audio engine's bookkeeping. They
 * keep their live values. */
bool StatePointers_Excluded(const char* var);

/* The pointers in the globals a state saves (malloc'd, caller frees; *out is
 * NULL when there are none). */
int StatePointers_Collect(StateReloc** out);

/* Writes the pointer back. A target that no longer exists leaves null. */
bool StatePointers_Apply(const StateReloc* r);
