#pragma once
/* Scene recorder (scene_rec_3ds.c, docs/debug-tools.md): the top screen, full size, into a RAM
 * ring; written to debug/tmc-scene-NNNN.bin on stop. Debug builds only. */
#include <stdbool.h>
#include <stdint.h>

bool SceneRec_Active(void);
/* every: keep one presented frame in N (1-6). */
bool SceneRec_Start(int every, char* reply, unsigned cap);
/* The ring is written at the next presented frame (the game stops while it is). */
bool SceneRec_Stop(char* reply, unsigned cap);
/* Once per presented frame, after it is on screen. */
void SceneRec_Tick(uint32_t presentedFrame);
