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
| 1 | the title's light rays; its logo (sprites) and sword (BG2, priority 1) in front of them at 8 px | BG0, priority 2 (4 px) |
| 2 | sun rays of the Minish Woods and the barrel house (`LightRayManager`) | BG3, priority 0 |
| 4 | the leaves of the Minish Paths corridors (area 0x11) | BG1, priority 1 |
| 8 | the file select (cave BG3 at priority 3): its sprites (banner, slots, texts, preview, name entry buttons) and BG1 at priority 1 (hearts, the languages, the name's letters) at 4 px, the preview's panel (BG2, priority 2) at 2 | OBJ, BG1, BG2 |
| 16 | the Nintendo and Capcom logos (mode 0, BG2 on, no BG1/BG3) | BG2, priority 2, 4 px |
| 64 | the game over screen (`TASK_GAMEOVER`): the GAME OVER letters (sprites) at 10 px, the choices (BG0, priority 0) at 6, the stone panel (BG1, BG2) at 3 | OBJ, BG0, BG1, BG2 |
| 32 | the map hints (`SUBTASK_MAPHINT`, `SUBTASK_LOCALMAPHINT`): the grey that covers what is not unlocked and its frame are BG2 (screen plane, with the backdrop); the unlocked map is BG1 at 2 px, seen through the OBJ-window sprites, which follow BG1 (`Stereo3DS_ObjWindowFollows`); the markers are sprites at 5 px | BG2 0, BG1 2, OBJ 5 |

## What Link holds up

A sprite rather than a layer: the object Link lifts to throw (the carried entity's child while
`gPlayerState.heldObject` is 3, lifting, or 4, holding) and the treasure he shows when he gets one
(`LINK_HOLDING_ITEM`) stand 4 px in front (`HELD_DEPTH` in `stereo_3ds.c`, owner 2026-10-20). `port_draw.c`
marks their OAM entries (`IsHeldUp`, `virtuappu_mode1_obj_front_mark`, committed with the rest of the OAM
metadata); each sprite is a batch of its own in the GPU model, which carries the mark (`PpuGpu3DSBatch.held`), and
the renderer moves those batches by `Stereo3DS_HeldShift` and counts them as in front (`IsFrontBatch`). When a
layer on the screen plane is drawn over them on the same lines (tree tops) the frame is drawn whole for each eye
(reason 4). The software renderer does not move them.

## How it works

- `stereo_3ds.c`, once a frame (`Stereo3DS_BeginFrame` in `PresentFrame`): the setting, the slider,
  and whether any layer has a depth. `Stereo3DS_LayerShift(layer, priority, eye)` is a layer's move
  in view pixels: depth x slider, half to each eye (the left eye to the right for a positive depth,
  out of the screen).
- The GPU renderer (`port_ppu_gpu_3ds.c`, `PortPpuGpu3DS_DrawPrepared`): each batch of a layer with a
  depth is moved by its layer's shift through the shader's offset, in whole pixels, and its scissor
  with it (or the region edges cut what moved). The right eye has its own output
  (`PortPpuGpu3DS_OutputTextureRight`, made on first use) and shares the left's depth-stencil (a buffer
  of its own did not fit in the New 3DS's VRAM beside the presenter's right target). A frame is drawn
  one of three ways, written to the SD log as `stereo: eyes ... (N)` when it changes:
  - **over one base** (0, the usual one, `DrawEyesOverBase`): the layers on the screen plane are drawn
    once on the left output; the right output starts as a copy of it (one quad); then each eye draws
    only the layers in front with its shift. The stencil's blend-target mark is kept in two bits by
    the base (0x08 and 0x10), one per eye, so the eye drawn first does not leak into the other; the
    sprites in front reset the depth first (they are above every base sprite anyway).
  - **share one picture** (6): no batch of the frame is on a layer with a depth (the HUD hidden):
    drawn once, the presenter shows it to both eyes.
  - **drawn whole** (any other number, the reason): the whole frame twice, as before. When something
    on the screen plane is drawn after a layer in front on the same lines (4), an OBJ window moves
    (1, 2: the map hints), a sprite in front is semi-transparent (3: the GBA blends it with the
    background, never with the lower sprite the base would have under it), `gpu_scissor_mode` is 0
    (7), or the debug poke `gPpuGpu3DSEyesWhole 01` asks for it (5, to compare).

  The whole frame twice doubled the GPU's work: 9.0 ms a frame in 2D, 17.1 ms in 3D in the Deepwood
  Shrine's dark room, 12.6 ms over one base (PLAN, decisions log 2026-10-18). With no depth at all
  (slider down) it is drawn once and both eyes get it.
- The presenter (`platform_gpu_3ds.c`): the right eye's screen target is made on first use and
  linked to the screen, with `gfxSet3D(true)`, only while 3D is on (`UpdateStereoOutput`, between
  frames). The top is drawn the same way for each eye (`sEyeTarget`; the right eye skips the upload
  and the counters). The software renderer's frames go to both eyes unchanged.

## Not done yet

- Depths for real scenes: a table per scene (area/room, or the effect that is on) instead of the
  one table; the layers of the GBA priority above Link's sprites are the candidates.
- Edges: a moved layer uncovers a strip at one side of the view; draw a few pixels more there.
- The cost of the second eye on the Old 3DS (both renderers' timings with the slider up).
- An OPTIONS cell for players, once there is something to see.

## Boot splash

`Platform3DS_ShowSplash` (`platform_3ds.c`) draws the top logo into the gfx framebuffers before the game's
presenter exists: with the slider up the left eye's picture is moved 3 px right and the right eye's 3 px
left (the HUD's depth), and the top screen is swapped with `gfxScreenSwapBuffers(GFX_TOP, true)`.
**The second parameter is `hasStereo`**: with false libctru copies the left image to the right eye and the
splash stays flat whatever is drawn per eye. 3D is turned off again in `Platform3DS_BootAnimStop`, before
the presenter links the right eye itself. `tools/make_splash.py` makes `romfs/splash.rgb565` (logo cut to its
bounds and centred, a 14 px margin so the shift crops nothing).

## Minish Village: unfinished (branch `feat/village-3d`, not merged)

- The flowers and plants over everything (BG1 at priority 1 in area 1) hang in front; so do the leaf
  blocks they stand in. Those are on BG2 with the ground, and a layer is all the PPU can move, so while 3D
  is on `Port_WidescreenVillagePlots` copies their cells (palette bank 5, plus the dark outline tiles 521,
  526, 527, 533, 782, 783 that touch them) to BG3 at priority 1, from `DispCtrlSet` (`src/interrupts.c`),
  where the registers are latched; a copy at the VBlank hook lagged a tick behind BG2's streaming. Palette
  bank 6 is the ground.
- Still to do: the houses should go back, not forward. Their roofs and bodies are on BG2 with the ground,
  their eaves (the orange or blue edge below) on BG1 with the leaves that are raised, and the Minish at
  the doors are sprites. Lowering all of BG2 is quick but takes the ground with the houses; lowering only
  the houses needs their cells split out of both layers, and the four layers are taken (BG0 HUD, BG1 front,
  BG2 ground, BG3 the leaf blocks). The hanging fruits of the plants were not found (not in the room
  checked): ask the owner for the room.
