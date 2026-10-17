# Building the GPU frame ahead of the GPU

How the GPU renderer (`platform/3ds/source/port_ppu_gpu_3ds.c`) prepares a frame while the
GPU is still drawing the previous one, and what it must never write before the wait.

## The problem

`C3D_FrameBegin` waits until the GX queue of the previous frame (its command lists and its
display transfers) has finished. The renderer used to call it first and then build the frame
(`PortPpuGpu3DS_Preflight`, ~3 ms in the Deepwood Shrine's dark room), because the build wrote
the vertex buffer, the tile atlas and the view rings that the GPU was still reading. A frame
then cost GPU + build + recording, one after the other: the dark room in 3D stopped at 40 fps
(54 with the stereo fix) on the New 3DS.

## What the GPU reads, and who writes it

| Buffer | Written by | GPU reads it | Now |
|---|---|---|---|
| Vertices (1 MiB, linear) | the build: map slices kept across frames, per-band geometry, iris quads; the reset and stereo copy quads | the frame's draws | two buffers; a frame is built into the one the last *drawn* frame does not use |
| Indices (linear) | once at init | every draw | one, never written again |
| Tile atlas (512x512 RGBA5551) | tile decodes (`cache_decode_slot`), refreshes in place | atlas batches | the build decodes into a heap copy; `Commit` copies the dirty slots to the texture after the wait |
| View rings (8 x 256 KiB) | ring cells (`ring_decode_cell`, memo copies) | ring batches | heap copies likewise; the ring now records each written cell (`dirtyCells`) so only those are copied |
| Picture texture (file select cave) | `RefreshIndexedPicture` on a palette change | BG1 picture batches | moved to `Commit` |
| Picture texture (title) | `SetBg1Picture` when the picture changes | BG1 picture batches | unchanged: written before the frame, rarely (a scene change) |

The model never reads the atlas or the rings back (the ring memo copies from its own cached
pool), so building into copies changes nothing it computes.

### Map slices in two buffers

A background's map geometry lives in a fixed slice of the vertex buffer and is kept while its
tilemap does not change. With two buffers, a slice rebuilt into one is missing from the other.
Each slice has a generation (bumped when it is rebuilt, also by a build that then failed: the
model keeps it) and each buffer remembers which generation it holds; after a build, a slice the
frame did not rebuild is copied from the other buffer when this one is behind (64 KiB, the frame
after a rebuild only). Reading the buffer the GPU is drawing from is harmless.

## The frame now

1. `PortPpuGpu3DS_Preflight` (before the wait): build into the free vertex buffer and the copies,
   bring the slices up to date, flush the vertices.
2. `PlatformGpu3DS_BeginCustomTop` (`C3D_FrameBegin`): wait for the previous frame.
3. `PortPpuGpu3DS_Commit`: the command budget check (it needs the command buffer's offset of
   this frame), the dirty atlas slots and ring cells into the textures, their flushes, the
   indexed picture.
4. `PortPpuGpu3DS_DrawPrepared`: record the draws.

A frame that is built but not drawn (a failed commit, a software fallback) leaves its buffer
free, and the next one is built there again.

## Tried and dropped: handing the GPU the commands while recording

Recording the draws costs ~4 ms after the wait in a 3D dark room (two eyes, `PPU submit` in a
dump), so submitting what was recorded every K draws (`C3D_FrameSplit`) looked like the next
overlap. Measured on the New 3DS (48/10, 3D, without the stereo fix): one submission 45.4 fps,
every 64/128/255 draws 44.7/44.6/45.0, a submission right after `DrawPrepared` 45.4: no gain,
the GPU is the limit once the build is off its path. Removed.

citro3d's GX queue also has a fixed number of entries per frame, and libctru stops the process
(`svcBreak`) when it is full: every submission is an entry, as are `C3D_FrameEnd`'s transfers
and the bottom screen's. A submission every 32 draws of a 3D frame (~700 draws) froze the New
3DS (2026-10-18; Luma dump: prefetch abort, IFSR 0x2, in `GX_ProcessCommandList` under
`C3D_FrameSplit` from `PlatformGpu3DS_EndBottom`). Anything that splits a frame must stay a few
entries a frame.

## Debug switch

`tmc-link.py poke gPpuGpu3DSSerialBuild 01`: claim the frame before building, as before (00
builds ahead again), to time one against the other in the same build and scene.

## Memory

+1 MiB linear (the second vertex buffer), +2.5 MiB heap (the atlas and ring copies).
