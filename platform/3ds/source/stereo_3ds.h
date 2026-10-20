#pragma once
/* Stereoscopic 3D on the top screen (docs/stereo-3d.md). The game stays a flat picture; layers
 * the port gives a depth are drawn shifted apart for each eye. On unless stereo_3d_on is 0
 * (tmc3ds.ini, DEBUG -> SWITCHES -> STEREO 3D), and only while the 3D slider is up. */
#include <stdbool.h>

typedef enum Stereo3DSEye { STEREO3DS_LEFT, STEREO3DS_RIGHT } Stereo3DSEye;


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
/* The move of what Link holds up (a lifted pot, a treasure he got): sprites marked by the port (PpuGpu3DSBatch.held). */
float Stereo3DS_HeldShift(Stereo3DSEye eye);
/* Sprites in OBJ-window mode make a mask, not a picture: true when the mask follows another layer
 * (the shift of that layer and priority is the mask's), as in the map hints' unlocked map. */
bool Stereo3DS_ObjWindowFollows(unsigned* layer, unsigned* priority);
/* The depth (pixels at the full slider, positive: out of the screen) a layer and priority have in the
 * scene up now, whether or not the slider is up; for the debug tint views (PortPpuGpu3DS_SetTintMode). */
int Stereo3DS_LayerDepth(unsigned layer, unsigned priority);
/* Debug (DEBUG -> SWITCHES -> FORCE 3D, not saved): the slider reads fully up whatever it is, so the two eyes can
 * be captured (tmc-link.py shot) and a 2DS shows the depth in its debug views. */
void Stereo3DS_SetForce(bool on);
bool Stereo3DS_Forced(void);
