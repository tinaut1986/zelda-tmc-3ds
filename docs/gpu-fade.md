# Screen fades on the GPU

How a screen fade (fade in or out, to black or to white) reaches the screen, and
what is done by the CPU and what by the GPU.

## What the game does

`FadeVBlank` (`src/fade.c`, called at the end of `WaitForNextFrame`) turns each of the 32
palettes (16 BG, 16 OBJ, 16 colours each) of `gPaletteBuffer` into `PAL_RAM`:

```
out = table[brightness][ (factor * 2*c + bias) >> 10 ]      per channel, c = 5 bits
factor = 0x400 - intensity * 4        bias = intensity * color
```

`intensity` (`gUnk_020354C0[n].unk2`, 0..256) and `color` (`unk1`, 0..255: 0 black,
248/255 white) are per palette. With the default brightness (`gEwram[6] == 1`) the table is
the identity, so a palette is **blended toward grey `color` by `intensity/256`**.

That is the CPU's part and it stays: the software renderer, the save states and the
quick dump read `PAL_RAM`.

## What the GPU renderer does with it

A fade used to cost the GPU renderer everything: every bank changes every frame, so every
cell of the view rings and every tile was decoded again (about 1800 ring cells, 13 ms on an
Old 3DS; the castle-garden dialogue fell to 28 fps, issue #3).

Now, when every palette was faded by the same `(intensity, color)`:

1. `platform/3ds/source/fade_gpu_3ds.c` checks it: it recomputes the fade of the buffer the
   game's vblank used and compares it with the palettes the renderer would read, with the
   same arithmetic as `src/fade.c`. Any difference (a masked fade, a palette written
   straight to `PAL_RAM`, a non-default brightness) means "not uniform" and the frame uses
   `PAL_RAM` as before.
2. The frame is built from the **unfaded** palettes. The tiles are decoded once; while the
   fade runs, nothing is decoded again.
3. After the last batch, `DrawPrepared` draws one full-screen quad of grey `color`, with
   alpha `intensity/256`, over the finished picture (`port_ppu_gpu_3ds.c`). Blending is
   linear, so it commutes with the alpha blends the frame did itself, and the target's 5-bit
   channels quantise it as `PAL_RAM` did.

The presented frame therefore equals the one the CPU fade produced, within the one-step
tolerance GPU CHECK allows.

### Which palette buffer

`PAL_RAM` is written by the vblank *after* the frame's logic, so a presentation shows the
previous logic frame's buffer faded with the previous frame's parameters.
`Platform3DS_BeginFrameBoundary` calls `FadeGpu3DS_OnFrameBoundary`, which snapshots the
buffer and the parameters once per logic frame; the check uses the snapshot of the previous
boundary (then the current one). It does not matter that presentations are skipped on an
Old 3DS: the snapshots follow logic frames, not presentations.

### Things that are not on the GPU yet

- A fade masked to some palettes (`gFadeControl.mask`), or palettes with different
  parameters: CPU path.
- Brightness setting other than the default (the tables are not linear): CPU path.
- Mosaic fades (`FADE_MOSAIC`) are the renderer's per-band mosaic, not palette fades.

## Switches and diagnostics

- `tmc3ds.ini`: `gpu_fade=0` keeps fades on the CPU.
- `tmc-link.py poke gPpuGpu3DSFadeQuadDisable 01`: skips the full-screen quad (the
  picture then shows the unfaded colours), to time it.
- `tmc-link.py peek gFadeGpu3DSStats 64`: calls, no fade, not default brightness,
  mismatches, uniform fades used, then the palette and entry of the last mismatch.
- The perf recorder's `decodes`, `ring_cells` and `ring_memo_hits` columns
  (`docs/debug-tools.md`) show the redecode a fade used to cause.
