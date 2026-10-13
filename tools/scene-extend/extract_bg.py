#!/usr/bin/env python3
"""A text BG of a screen dump as a picture, and the template to extend it for a 400-wide view.

    extract_bg.py DUMP_PREFIX BG [OUTDIR] [--left N] [--right N] [--top N] [--bottom N]
                  [--width N] [--height N] [--sprites]

DUMP_PREFIX is a screen dump's files without their suffix (debug/tmc-dump-NNNN, fetched with
tmc-link.py dump and curl): it reads -vram.bin, -io-registers.bin, -palettes.bin (and -oam.bin
with --sprites). BG is 0-3. Writes to OUTDIR (default: the dump's folder):

  bgN-full.png       the whole BG map (256 or 512 wide, 256 or 512 tall), as VRAM holds it
  bgN-picture.png    the part with content, from the top-left corner to the last non-empty
                     column and row (or --width / --height)
  bgN-template.png   the picture in a transparent canvas: --left columns before it (default 80,
                     the GBA's 240 columns centred in 400), --right after it (default the rest of
                     400), --top rows above and --bottom below (default 0; 40 each to fill 240
                     lines around a 160-line screen): what the generative AI fills
  bgN-with-sprites.png  (--sprites) the picture with the dump's sprites at the BG's scroll, for context

Process and prompt: docs/wide-scene-extension.md. The outputs are the game's art: keep them out
of the repository.
"""
import argparse
import os
import struct

from PIL import Image

SIZES = [(256, 256), (512, 256), (256, 512), (512, 512)]


def rgb(pal, i):
    c, = struct.unpack_from('<H', pal, i * 2)
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def render_bg(vram, io, pal, bg):
    cnt, = struct.unpack_from('<H', io, 8 + 2 * bg)
    cb, sb = ((cnt >> 2) & 3) * 0x4000, ((cnt >> 8) & 31) * 0x800
    c256, (w, h) = (cnt >> 7) & 1, SIZES[cnt >> 14]
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = img.load()
    for ty in range(h // 8):
        for tx in range(w // 8):
            block = (tx // 32) + (ty // 32) * (w // 256)
            e, = struct.unpack_from('<H', vram, sb + block * 0x800 + ((ty % 32) * 32 + (tx % 32)) * 2)
            t, hf, vf, pb = e & 0x3ff, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            for y in range(8):
                for x in range(8):
                    sx, sy = (7 - x if hf else x), (7 - y if vf else y)
                    if c256:
                        o = cb + t * 64 + sy * 8 + sx
                        i = vram[o] if o < len(vram) else 0
                    else:
                        o = cb + t * 32 + sy * 4 + sx // 2
                        b = vram[o] if o < len(vram) else 0
                        i = (b >> (4 * (sx & 1))) & 15
                        i = pb * 16 + i if i else 0
                    if i:
                        px[tx * 8 + x, ty * 8 + y] = rgb(pal, i) + (255,)
    return img, cnt


def draw_sprites(img, vram, pal, oam, scroll_x, scroll_y):
    dims = {(0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64), (1, 0): (16, 8),
            (1, 1): (32, 8), (1, 2): (32, 16), (1, 3): (64, 32), (2, 0): (8, 16), (2, 1): (8, 32),
            (2, 2): (16, 32), (2, 3): (32, 64)}
    px = img.load()
    for n in range(127, -1, -1):
        a0, a1, a2 = struct.unpack_from('<HHH', oam, n * 8)
        affine = (a0 >> 8) & 1
        if (not affine and (a0 >> 9) & 1) or (a0 >> 14) == 3:
            continue
        w, h = dims[(a0 >> 14, a1 >> 14)]
        y, x = a0 & 0xff, a1 & 0x1ff
        y, x = (y - 256 if y >= 160 else y), (x - 512 if x >= 256 else x)
        if affine and (a0 >> 9) & 1:
            x, y = x + w // 2, y + h // 2
        c256, tile, pb = (a0 >> 13) & 1, a2 & 0x3ff, a2 >> 12
        hf, vf = (not affine) and (a1 >> 12) & 1, (not affine) and (a1 >> 13) & 1
        for yy in range(h):
            for xx in range(w):
                sx, sy = (w - 1 - xx if hf else xx), (h - 1 - yy if vf else yy)
                tx, ty = sx // 8, sy // 8
                if c256:
                    o = 0x10000 + (tile + (ty * (w // 8) + tx) * 2) * 32 + (sy % 8) * 8 + sx % 8
                    i = vram[o] if o < len(vram) else 0
                    i = 256 + i if i else 0
                else:
                    o = 0x10000 + (tile + ty * (w // 8) + tx) * 32 + (sy % 8) * 4 + (sx % 8) // 2
                    b = vram[o] if o < len(vram) else 0
                    i = (b >> (4 * (sx & 1))) & 15
                    i = 256 + pb * 16 + i if i else 0
                X, Y = x + xx + scroll_x, y + yy + scroll_y
                if i and 0 <= X < img.width and 0 <= Y < img.height:
                    px[X, Y] = rgb(pal, i) + (255,)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('dump')
    ap.add_argument('bg', type=int)
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--right', type=int)
    ap.add_argument('--height', type=int)
    ap.add_argument('--width', type=int)
    ap.add_argument('--top', type=int, default=0)
    ap.add_argument('--bottom', type=int, default=0)
    ap.add_argument('--sprites', action='store_true')
    args = ap.parse_args()
    read = lambda suffix: open(args.dump + suffix, 'rb').read()
    vram, io, pal = read('-vram.bin'), read('-io-registers.bin'), read('-palettes.bin')
    out = args.outdir or os.path.dirname(args.dump) or '.'
    os.makedirs(out, exist_ok=True)
    full, cnt = render_bg(vram, io, pal, args.bg)
    full.save(os.path.join(out, 'bg%d-full.png' % args.bg))
    bbox = full.getbbox() or (0, 0, full.width, full.height)
    width = args.width if args.width is not None else bbox[2]
    height = args.height if args.height is not None else bbox[3]
    picture = full.crop((0, 0, width, height))
    picture.save(os.path.join(out, 'bg%d-picture.png' % args.bg))
    right = args.right if args.right is not None else 400 - args.left - width
    template = Image.new('RGBA', (args.left + width + right, args.top + height + args.bottom), (0, 0, 0, 0))
    template.paste(picture, (args.left, args.top))
    template.save(os.path.join(out, 'bg%d-template.png' % args.bg))
    print('BG%d control %04x: %dx%d picture, template %dx%d (left %d, right %d, top %d, bottom %d)' %
          (args.bg, cnt, width, height, template.width, template.height, args.left, right, args.top, args.bottom))
    if args.sprites:
        hofs, vofs = struct.unpack_from('<HH', io, 0x10 + 4 * args.bg)
        ctx = picture.copy()
        draw_sprites(ctx, vram, pal, read('-oam.bin'), hofs & 0x1ff if hofs & 0x1ff < 256 else (hofs & 0x1ff) - 512,
                     vofs & 0x1ff if vofs & 0x1ff < 256 else (vofs & 0x1ff) - 512)
        ctx.save(os.path.join(out, 'bg%d-with-sprites.png' % args.bg))


if __name__ == '__main__':
    main()
