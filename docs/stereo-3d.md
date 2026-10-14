# Stereoscopic 3D

The game stays a flat GBA picture. In 3D the port moves chosen layers apart for each eye, so what
lies over the playfield (fog, the leaves over the Minish village) can sit in front of the screen.
It is off unless asked for (owner, 2026-10-14): which layers get a depth, and where, is the owner's
call, scene by scene. The structure is in place; every depth is 0.

## When it is on

`stereo_3d` in `tmc3ds.ini` (debug builds: DEBUG -> SWITCHES -> STEREO 3D):

| Value | What |
|---|---|
| 0 OFF | 2D, as always (the default) |
| 1 ON | 3D with the depths of `kDepth` (`stereo_3ds.c`): all 0 for now, so both eyes see the same picture |
| 2 TEST | 3D with `kTestDepth`: BGs at priority 0 and the sprites pop out, to see that both eyes work |

3D is on only while the slider is up (`osGet3DSliderState() > 0`); a 2DS always reads 0 and stays
2D. Changes of the setting or of 3D on/off are written to the SD log (`stereo: setting N, slider
S, 3D on|2D`).

## How it works

- `stereo_3ds.c`, once a frame (`Stereo3DS_BeginFrame` in `PresentFrame`): the setting, the slider,
  and whether any layer has a depth. `Stereo3DS_LayerShift(layer, priority, eye)` is a layer's move
  in view pixels: depth x slider, half to each eye (the left eye to the right for a positive depth,
  out of the screen).
- The GPU renderer (`port_ppu_gpu_3ds.c`, `PortPpuGpu3DS_DrawPrepared`): with a depth, the prepared
  frame is drawn twice, the second time on its own output (`PortPpuGpu3DS_OutputTextureRight`,
  made on first use), each batch moved by its layer's shift through the shader's offset. With no
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
- Windows (scissor) do not move with their layer.
- The cost of the second draw on the Old 3DS (both renderers' timings with the slider up).
- An OPTIONS cell for players, once there is something to see.
