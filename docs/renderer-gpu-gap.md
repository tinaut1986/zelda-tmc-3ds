# GPU renderer: what Minish Cap needs, against mzm's renderer

Analysis for PLAN P3 (2026-10-11). Question: can `../mzm`'s GBA GPU renderer
(`../mzm/platform/3ds/source/port_gpu_renderer.c`, 60 fps with a 400x240 wide view on an
Old 3DS) replace ours (upstream's, `platform/3ds/source/port_ppu_gpu_3ds*.c`), which
spends 10.3 ms building a frame on a New 3DS (PLAN, baseline)?

## What the game uses

From `src/` (zeldaret/tmc decompilation):

| Feature | Where in Minish Cap |
|---|---|
| Mode 0, four tiled BGs | almost everything |
| Mode 1, affine BG2 | the title screen (`src/title.c`); the Deepwood Shrine rolling barrel (`src/manager/rollingBarrelManager.c`) |
| Registers changed per scanline (HBlank DMA, emulated by the port's HDMA) | `WIN0H`: circular and shaped windows (`src/common.c`: iris transitions, `lightManager` dark rooms, `whiteTriangleEffect`); `BG3HOFS`: ripples (`steamOverlayManager`, `lightRayManager`, `vaatiAppearingManager`); `BG3CNT`: pause menu (`pauseMenuScreen6.c`); `BG2PA`: the barrel's cylinder |
| BG mosaic | `FADE_MOSAIC` fades (`src/fade.c`) |
| Alpha blend, brighten, darken | 34 files (`BLDCNT`/`BLDALPHA`) |
| Affine OBJ | 27 files |
| Full View (Wide + Pixel Perfect) | BG columns past 240 come from the port's shadow tilemaps (`virtuappu_mode1_ws_shadow`, `port/ppu/include/cpu/mode1.h`), not from VRAM |

## Coverage

| Feature | mzm's renderer | ours |
|---|---|---|
| Mode 0 BGs, OBJ, affine OBJ | yes | yes |
| Alpha / brighten / darken | yes, one setting per frame | yes, per scanline band |
| Affine BG2 | only a pure scale on a 256x256 map | yes |
| Per-scanline registers | no (two effects special-cased: a BG3 ripple and the power-bomb window) | yes (scanline bands) |
| Windows | one rectangle (WIN0 or WIN1) plus OBJWIN; both windows clipping at once falls back | yes, per band, both windows |
| BG mosaic | falls back | yes |
| Wide view | yes, from VRAM | normal Wide yes (shadow tilemaps); Full View no (the policy refuses it: `PpuGpu3DS_ShouldUse`) |

## Conclusion

mzm's renderer cannot be dropped in: per-scanline windows and scrolls, affine BG2 and
mosaic are everyday Minish Cap (every iris transition, dark rooms, ripples), and mzm has
none of them in general form. Ours already covers every feature the game uses; what it
lacks is speed and Full View.

So the plan is to keep our feature model and change how it builds, taking the parts that
made mzm and sm-3ds fast:

1. **Measure the build first.** The builder's phase timers (`PPU_GPU3DS_PROFILE`) were
   compiled into the console build but never reported; the SD log now prints them every
   5 s (`build:` line). One Hyrule Field recording says which phases make the 10.3 ms.
2. **Overlap CPU and GPU.** Today the frame waits for the previous GPU frame (6.3 ms)
   before it builds, because the build rewrites buffers the GPU is still reading.
   Double-buffering them (as mzm does) runs the two in parallel.
3. **Fewer, bigger BG quads.** Ours emits one quad per 8x8 tile. mzm draws one quad
   per 16x16 block (step A in `../mzm/docs/3ds-renderer-perf-plan.md`: 45 to 60 fps in
   its worst room); sm-3ds (SNES, ideas only) decodes each tilemap into a texture kept
   current by dirty tracking and draws one quad per scroll run (~3 ms of build on a 2DS).
   Which one depends on step 1.
4. **Full View on the GPU** (P3.4): read the shadow tilemaps for columns past 240 and
   lift the 240x160 limits, once 1-3 leave room for 2.5 times the pixels.

mzm's lessons apply whichever way it goes: measure with a runtime toggle before
believing a cost model (three of theirs died that way), flush the vertex cache once per
frame, and invalidate the texture cache after an in-place atlas redecode.
