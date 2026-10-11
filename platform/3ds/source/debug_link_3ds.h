#pragma once
/* Remote control of the running game for debug-tools builds (PLAN P1.10,
 * docs/debug-tools.md "Remote control"), carried as SITE commands over the
 * in-game FTP server so there is one port and one switch:
 *
 *   SITE KEYS <A+B+LEFT|NONE> [frames]   hold 3DS buttons for N logic frames (default 1)
 *   SITE TOUCH <x> <y>                    tap the bottom screen
 *   SITE SHOT                             write debug/shot-top.bmp and debug/shot-bottom.bmp
 *   SITE SAVESTATE [n]                    a new save state, or over number n (save_state_3ds.h)
 *   SITE LOADSTATE [n]                    load save state n (1 = newest, the default)
 *   SITE DELSTATE <n>                     delete save state n
 *   SITE STATES                           the list: n=area/room ...
 *   SITE LOADDUMP                         load the newest quick dump's save (upstream's LOAD STATE)
 *   SITE DUMP                             take a quick dump (as L+R+A)
 *   SITE SWITCH <name>                    cycle a DEBUG -> SWITCHES value
 *   SITE STATE                            one line: frame, fps, in game, area, room, Link, health
 *   SITE PEEK <address> <length>          hex of up to 256 bytes of this process's memory
 *   SITE POKE <address> <hex bytes>       write them (main thread, between frames)
 *
 * tools/tmc-link/ is the PC client. Anything that touches the game runs on the
 * main thread between frames (DebugLink_Poll); the FTP thread waits for it. */
#include <stdbool.h>
#include <stdint.h>

/* FTP thread. Handles `verb` (upper case) with `args`; writes the whole reply
 * line, code included, into `reply`. False when the verb is not ours. */
bool DebugLink_Command(const char* verb, const char* args, char* reply, unsigned cap);

/* Main thread, from the input poll: adds injected buttons to `held` / `down`
 * (3DS KEY_* masks) and runs the queued request, if any. */
void DebugLink_Poll(uint32_t* held, uint32_t* down, uint64_t logicFrame);
