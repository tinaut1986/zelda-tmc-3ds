#pragma once
/* OPTIONS -> FRAMES (frame_pacing in tmc3ds.ini), as in ../mzm and ../sm-3ds.
 * AUTO: skip drawing a frame when the last one ran late; the game keeps its
 * 60 Hz (the Old 3DS adaptive pacer, used on both models). LOCK 30: draw
 * every other frame; the game keeps its 60 Hz. NO SKIP: draw every frame;
 * when that is too slow, the game slows down with it.
 * Kept apart from port_runtime_config.h, whose GBA types clash with libctru's. */
enum { PORT_3DS_PACING_AUTO = 0, PORT_3DS_PACING_LOCK30, PORT_3DS_PACING_NO_SKIP, PORT_3DS_PACING_COUNT };
int Port_Config_GetFramePacing(void);
void Port_Config_SetFramePacing(int pacing);
