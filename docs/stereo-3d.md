# Stereoscopic 3D

The game stays a flat GBA picture. In 3D the port moves chosen layers apart for each eye, so what
lies over the playfield (fog, the leaves over the Minish village) can sit in front of the screen.
It is always on (owner, 2026-10-15): the player lowers the console's 3D slider to get the flat
picture. Which layers get a depth, and where, is the owner's call, scene by scene. The HUD and the
text boxes sit in front (owner, 2026-10-14).

## When it is on

3D is on while the slider is up (`osGet3DSliderState() > 0`); a 2DS always reads 0 and stays 2D.
`stereo_3d_on` in `tmc3ds.ini` (1 by default; debug builds: DEBUG -> SWITCHES -> STEREO 3D, ON/OFF)
turns it off for good. The depths are `kDepth` in `stereo_3ds.c`: BG0 at priority 0 (HUD, text
boxes) and the priority-0 sprites (the HUD's buttons) 6 px in front; Link and the world on the
screen plane. Changes of the setting or of 3D on/off are written to the SD log (`stereo: setting N,
slider S, 3D on|2D`).

## Scenes with a depth

Besides `kDepth`, `Stereo3DS_BeginFrame` asks `Port_Stereo_Scenes` (`port_linked_stubs.c`) which
scenes are up, from the layers the game has on; each puts one layer at 4 px (HUD stays at 6, in front).
Written to the SD log as `stereo: scenes N` when it changes.

| Bit | Scene | Layer |
|---|---|---|
| 1 | the title's light rays | BG0, priority 2 |
| 2 | sun rays of the Minish Woods and the barrel house (`LightRayManager`) | BG3, priority 0 |
| 4 | the leaves of the Minish Paths corridors (area 0x11) | BG1, priority 1 |

## How it works

- `stereo_3ds.c`, once a frame (`Stereo3DS_BeginFrame` in `PresentFrame`): the setting, the slider,
  and whether any layer has a depth. `Stereo3DS_LayerShift(layer, priority, eye)` is a layer's move
  in view pixels: depth x slider, half to each eye (the left eye to the right for a positive depth,
  out of the screen).
- The GPU renderer (`port_ppu_gpu_3ds.c`, `PortPpuGpu3DS_DrawPrepared`): with a depth, the prepared
  frame is drawn twice, the second time on its own output (`PortPpuGpu3DS_OutputTextureRight`,
  made on first use), each batch moved by its layer's shift through the shader's offset, in whole
  pixels, and its scissor with it (or the region edges cut what moved). The right eye shares the
  left's depth-stencil (a buffer of its own did not fit in the New 3DS's VRAM beside the
  presenter's right target); both target clears run before either eye's commands, so the quad that
  resets the stencil at the start of each draw resets the depth too. With no
  depth it is drawn once and both eyes get it: 3D ON with the depths at 0 costs nothing in the
  renderer.
- The presenter (`platform_gpu_3ds.c`): the right eye's screen target is made on first use and
  linked to the screen, with `gfxSet3D(true)`, only while 3D is on (`UpdateStereoOutput`, between
  frames). The top is drawn the same way for each eye (`sEyeTarget`; the right eye skips the upload
  and the counters). The software renderer's frames go to both eyes unchanged.

## Not done yet

- Depths for real scenes: a table per scene (area/room, or the effect that is on) instead of the
  one table; the layers of the GBA priority above Link's sprites are the candidates.
- Edges: a moved layer uncovers a strip at one side of the view; draw a few pixels more there.
- The cost of the second draw on the Old 3DS (both renderers' timings with the slider up).
- An OPTIONS cell for players, once there is something to see.
