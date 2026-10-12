/* Reserved space for the save state symbol table.
 *
 * Zero in the object file; tools/gen_state_symbols.py fills it in the linked
 * ELF (see state_symbols_3ds.h). volatile: the compiler must not fold reads
 * of it to zeros, since nothing in C ever writes it. */
#include "state_symbols_3ds.h"

__attribute__((used, aligned(4)))
const volatile unsigned char gStateSymbolBlob[STATE_SYMBOL_BLOB_SIZE] = { 0 };
