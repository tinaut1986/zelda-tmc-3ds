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

## The world while playing

While playing (`TASK_GAME`, `GAMEMAIN_UPDATE`; scene bit 128, `PORT_STEREO_SCENE_WORLD`) the layer drawn over Link
(BG1: tree tops, eaves, roofs) stands out of the screen by `stereo_world_bg1` px (default 2) and Link and the world's
sprites (priority 1-3; the HUD's are 0 and keep `kDepth`'s 6) by `stereo_world_obj` px (default 0: on the ground a sprite stays on the ground's plane, so it does not float). A sprite raised off the
ground (Link jumping, a flying enemy) stands out with its height instead: `stereo_world_height` percent of it (default 25:
16 px up is 4 px out, at most 5), through the per-sprite depth below (`SpriteDepth`). The pieces of a tall sprite (more than one OAM entry) stand out with
their height over the lowest piece, the feet, which stays on the plane (`stereo_world_body` percent, default 8: a piece
16 px up is 5 quarter pixels out, at most 3 px), so Link and the characters lean out of the ground instead of floating. The ground (BG2)
and the sky (BG3) stay on the screen plane. `stereo_world_on=0` in `tmc3ds.ini` (or OPTIONS -> 3D WORLD, OFF / ON) switches both off; a value of 0
switches that part off (sprites at 0 keep the frame drawn over one base, which costs less on the Old 3DS). The other
scenes below override it for their layers. The values are in pixels at the full slider, -16 to 16.

### What was added on top of PR #47 (feat/stereo-world, local)

- **Only in the world scene.** The height and body depths of a sprite (`Port_Config_StereoHeightDepthQ` / `StereoBodyDepthQ`)
  apply only while `Stereo3DS_WorldScene()` (scene bit 128 and the slider up): a sprite with a depth of its own ignores its
  layer's, so in the title, the file select and GAME OVER they flattened what those scenes tune by hand.
- **Lean per tile row.** Each OAM entry carries the depth at its bottom (`virtuappu_mode1_obj_depth`) and at its top edge
  (`virtuappu_mode1_obj_depth_top`); `build_obj` gives each row of tiles (8 px) the depth in between and batches runs of equal
  depth (at most 20 per sprite, `SPRITE_LEAN_3DS_MAX_RUNS`, else one depth, the highest; sprites of up to 64 tiles). The
  per-tile depths are `SpriteLean3DS_Fill` (`platform/3ds/source/sprite_lean_3ds.h`, host test
  `platform/3ds/tests/sprite_lean_3ds_test.c`).
- **Support.** A sprite takes the depth of the background layer it rests on (the roof under a statue): `compute_obj_support`
  samples the frontmost opaque BG1-BG3 layer behind it (priority at or after its own) at the middle of each tile with
  `text_bg_sample`, takes the highest of the entry's tiles and the highest of the entries of one entity
  (`virtuappu_mode1_obj_group`, the entity that wrote them). A layer in front of the sprite gives nothing.
- **The HUD is in front.** Sprites at priority 0 take nothing from their pieces' height and the world's sum is capped at
  5 px, under the HUD's 6.
- **Strips a moved layer leaves.** BG1-BG3 in front move one pixel per eye; where the art relies on them covering a hole
  (a door's arch over ground that has none) or the outline of the layer below, a black line showed at one side, the other
  in the other eye. In the world scene each eye draws such a layer twice, the unmoved copy first (`IsGhostBatch`): the strip
  shows the layer's own pixels, so its edge is a pixel thicker on that side.
- PLANE TINT -> DEPTH paints a sprite by its own depth.
- **A question at the start** (`stereo_world_asked`, `Port_Config_StereoWorldQuestionDue`, after the WIDE + AI one): 3D WORLD is off
  until the player has been asked once. The game was drawn for flat 2D and some scenes show graphic errors that are hard to fix, the
  message says so and asks whether to turn it on anyway. The answer is kept; OPTIONS -> 3D WORLD changes it and counts as an answer.
- **Sprite depth corrections** (`platform/3ds/source/port_sprite_depth.inc`, optional, hand-curated, in the manner of the
  sibling ports): `PORT_SPRITE_DEPTH(kind, id, type, mode)` per kind of entity. The mode says where the sprite meets the
  ground; the depth is nil there and grows across the sprite by the normal rule's amount (`Port_Config_StereoBodyDepthQ` of
  the distance from that edge, 12 quarters at most, 20 with its height). `PORT_DEPTH_UP` (0) is the normal rule, what an
  entity without an entry gets: ground at the bottom of its pieces, the depth grows upwards. `PORT_DEPTH_DOWN` (alias
  `PORT_DEPTH_INVERT`): a sprite that hangs from its top edge (the boss door in a south wall) has its ground at the top of
  its pieces and the depth grows downwards (the pieces carry a bottom depth higher than the top one, and the model's rows
  follow either way). `PORT_DEPTH_LEFT`: a sprite on a wall to its left (the curtain, `GENTARI_CURTAIN`, on the west wall of
  area 0x20 room 00) has its ground at the right edge of its pieces and the depth grows towards the left of the screen;
  `PORT_DEPTH_RIGHT` is the mirror. A side lean is written per OAM entry as the depth at its right edge (`obj_depth`) and at
  its left edge (`obj_depth_top`, with bit 7, `SPRITE_LEAN_3DS_COLUMNS`, set on every piece of the sprite, even one whose
  edges are equal, and bit 6, `SPRITE_LEAN_3DS_RISES_RIGHT`, for `PORT_DEPTH_RIGHT`); `build_obj` gives each column of tiles the
  depth at its middle, in screen coordinates (an h-flipped entry has its columns reversed, an affine one is measured where
  the middle of each tile lands), and draws such an entry column by column, so its runs of equal depth are its columns (8 at
  most). An affine entry rotated off the axes makes more runs and may hit the cap (one depth, the highest). Each run keeps
  the entry's scissor moved by its own shift, as a row lean does. Its batches carry the side (`PpuGpu3DSBatch.side`) and
  the renderer gives each the whole disparity in one eye and none in the other (`SpriteLean3DS_EyeShift`, from
  `SpritePx`): with the normal split two neighbouring columns (of one entry or of two) that rounded to different shifts
  parted by a pixel in the eye that moves the higher one away from the lower one, a dark seam down the curtain in the
  right eye (owner, 2026-10-22). Now the fixed eye has every column at 0 and the moving one is the eye that takes the
  higher columns towards the ground edge, so neighbours overlap (by the step of their disparity; the normal split
  already overlapped them in that eye) and never part, at any slider, in or out of the screen. The disparity of each
  column, which is what is seen as depth, is the same as before; the sprite's middle sits half of it (a pixel or two at
  most) towards the ground edge. Rows keep the normal split: they only move sideways, so they never part. The host test
  (`platform/3ds/tests/sprite_lean_3ds_test.c`) measures the gap between every pair of neighbouring columns, both
  eyes, every slider from -1 to 1. `PORT_DEPTH_FLAT`: no lean. `PORT_DEPTH_RAISED` (5): no lean either, the whole sprite at
  one depth, the one the normal rule gives its top edge: the entity's own depth plus `Port_Config_StereoBodyDepthQ` of its
  whole height (feet to the top of its highest piece, 12 quarters at most), 20 quarters at most
  (`SpriteLean3DS_RaisedDepth`), even for a sprite the normal rule would not lean (one piece 16 px tall or less). Every
  piece carries it at both edges, so each is one batch (affine and flipped ones too). For the small archway in a wall
  (`MINISH_SIZED_ARCHWAY` type 2, left of the curtain in area 0x20 room 00). Every mode leaves the HUD's sprites
  (priority 0) alone. To find the kind and id of a sprite on screen, read the OAM entry's group (`virtuappu_mode1_obj_group`) against the
  entity that wrote it (kind, id and type are in `Entity`); BOSS_DOOR is `OBJECT`, 57.
- **The big Chuchu** (`feat/stereo-boss`): it is three entities stacked 14-18 px apart, each piece of them a rotated or scaled
  (affine) 64 px sprite. Its ground is the lowest bottom edge among its pieces (the frame before), and the rise from feet to
  head is spread over its whole height (`sBossGround`/`sBossTop` in `port_draw.c`, 16 quarters at most; its jump counts 4 at
  most, so the ramp survives a jump). Affine pieces are cut in rows of tiles by where the middle of each tile lands on the
  screen. The piece that touches the ground (its shadow) stays at 0, and every piece of the boss is drawn with the layers in
  front (`PpuGpu3DSBatch.front`, bit 7 of the OAM entry's depth) so the base/front split cannot invert what covers what.
  Its batches are not depth tested (`IsPainterBatch`): they skip the depth pre-pass and are drawn with the depth test off, in the
  order of the list (back to front). With the pre-pass, the colour pass of a sprite drew only where the depth was equal, and with
  the boss cut into many small batches some of those draws were lost (two rows of tiles of its legs, in one eye, in ~4 % of the
  frames; 0 of 116 with the change, 5 of 122 without, alternating the two on the console).

## What Link holds up or throws

A sprite rather than a layer. The treasure Link shows when he gets one (`LINK_HOLDING_ITEM`) stands 4 px in
front; the object he lifts to throw (the carried entity's child while `gPlayerState.heldObject` is 3, lifting, or 4,
holding) stands out by its height over the floor: 4 px over his head (the height is learnt while he holds it), less
while it rises, and once thrown it is followed (`sThrown`, same kind and id) and comes down with its height until it
lands (z 0) and breaks (owner, 2026-10-20). `port_draw.c` (`SpriteDepth`) writes each OAM entry's depth in quarter
pixels to `virtuappu_mode1_obj_depth`, committed with the rest of the OAM metadata; each sprite is a batch of its own in
the GPU model, which carries it (`PpuGpu3DSBatch.depth`), and the renderer moves the batch and its scissor by
`Stereo3DS_SpriteShift` (`SpritePx`) and counts it as in front when that rounds to a pixel or more (`IsFrontBatch`).
The scissor must move with the sprite's own shift: with its layer's (0) a two-piece jar split down the middle. When a
layer on the screen plane is drawn over such a sprite on the same lines (tree tops) the frame is drawn whole for each
eye (reason 4). The software renderer leaves them flat.

## How it works

- `stereo_3ds.c`, once a frame (`Stereo3DS_BeginFrame` in `PresentFrame`): the setting, the slider,
  and whether any layer has a depth. `Stereo3DS_LayerShift(layer, priority, eye)` is a layer's move
  in whole view pixels: depth x slider, rounded and split between the eyes (the left eye to the right for a positive
  depth, out of the screen; an odd pixel goes to the left eye, so the smallest disparity is 1 px).
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
    (1, 2; the map screens' window follows their grey, BG2, which stays on the screen plane, so it does
    not move and they are drawn over one base), a sprite in front blends (3: the GBA blends it with the
    background, never with the lower sprite the base would have under it; a semi-transparent sprite with
    no second target in BLDCNT does not blend and does not count), `gpu_scissor_mode` is 0
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

## Minish Village: unfinished (the `feat/village-3d` branch was deleted on 2026-10-22; what follows is what it found)

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
