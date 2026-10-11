#pragma once
/* SD log for debug-tools builds (docs/debug-tools.md, "SD log").
 *
 * debug/tmc-log-NNNN.txt, one per logging session. BUFFERED (the default):
 * lines collect in a 16 KB RAM buffer written a block at a time, when it fills,
 * on a mark, when logging or buffering is switched off and on exit. DIRECT:
 * each line is written and flushed at once, slower, but a hang cannot swallow
 * the last lines. While it runs, every 5 s a perf line is added
 * (DebugLog_Tick). Thread-safe. */
#include <stdbool.h>

void DebugLog_Start(void);
void DebugLog_Stop(void);
bool DebugLog_Running(void);
void DebugLog_SetDirect(bool direct);
bool DebugLog_Direct(void);
/* The current file's number, 0 when none. */
int DebugLog_FileNumber(void);
void DebugLog_Printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
/* A numbered marker line, flushed at once: "--- MARK n ---". */
int DebugLog_Mark(void);
/* Once per presented frame: adds the 5 s perf line when it is due. */
void DebugLog_Tick(void);
void DebugLog_Flush(void);
