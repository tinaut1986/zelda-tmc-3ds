#pragma once
/* Stereoscopic 3D on the top screen (docs/stereo-3d.md). The game stays a flat picture; layers
 * the port gives a depth are drawn shifted apart for each eye. Off unless stereo_3d is set
 * (tmc3ds.ini, DEBUG -> SWITCHES -> STEREO 3D), and only while the 3D slider is up. */
#include <stdbool.h>

typedef enum Stereo3DSEye { STEREO3DS_LEFT, STEREO3DS_RIGHT } Stereo3DSEye;

/* stereo_3d: off, on, or on with the test depths (Stereo3DS_LayerShift). */
enum { STEREO3DS_OFF, STEREO3DS_ON, STEREO3DS_TEST, STEREO3DS_SETTING_COUNT };

/* Once a frame, before it is built: reads the setting and the slider. */
void Stereo3DS_BeginFrame(void);
/* The top screen shows an image per eye this frame. */
bool Stereo3DS_Active(void);
/* Some layer is off the screen plane this frame, so the renderer draws a picture per eye;
 * otherwise both eyes show the same one. */
bool Stereo3DS_HasDepth(void);
/* How far, in view pixels, a GBA layer (PPU_GPU3DS_BG0..BG3, PPU_GPU3DS_OBJ) at a priority
 * (0 front .. 3 back) is moved for an eye: depth (positive: out of the screen) times the
 * slider, half to each eye, the left eye to the right. 0 when not active. */
float Stereo3DS_LayerShift(unsigned layer, unsigned priority, Stereo3DSEye eye);
