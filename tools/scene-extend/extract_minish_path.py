#!/usr/bin/env python3
"""The two parallax layers of a vertical Minish Path (Minish Paths, area 0x11) as pictures.

    extract_minish_path.py DUMP_PREFIX [OUTDIR]

DUMP_PREFIX is a screen dump of the corridor (tmc-link.py dump, fetched by name): it reads
-map-top-special.bin (the game's pre-built parallax maps: BG3 at byte 0, BG1 at 0x2000, 32 columns
x 128 rows each, see src/manager/verticalMinishPathBackgroundManager.c), -vram.bin (character
block 2) and -palettes.bin. Writes to OUTDIR (default: the dump's folder):

  bg1-leaves-full.png    BG1, the leaves, 256 x 1024 (columns 30-31 are never inside the GBA's 240)
  bg3-shadows-full.png   BG3, the shadows, same size
  bg1-leaves-strip.png, bg3-shadows-strip.png
                         the template the generative AI fills: the layer's 240 columns centred in
                         400 (80 transparent on each side), as tall as the picture has content

The outputs are the game's art: keep them out of the repository (docs/wide-scene-extension.md).
"""
import os
import struct
import sys

from PIL import Image


def render(tiles, pal, vram, cb, rows=128, cols=32):
    img = Image.new('RGBA', (cols * 8, rows * 8), (0, 0, 0, 0))
    px = img.load()
    for ty in range(rows):
        for tx in range(cols):
            e, = struct.unpack_from('<H', tiles, (ty * cols + tx) * 2)
            t, hf, vf, pb = e & 0x3ff, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            for y in range(8):
                for x in range(8):
                    sx, sy = (7 - x if hf else x), (7 - y if vf else y)
                    o = cb + t * 32 + sy * 4 + sx // 2
                    b = vram[o] if o < len(vram) else 0
                    i = (b >> (4 * (sx & 1))) & 15
                    if not i:
                        continue
                    c, = struct.unpack_from('<H', pal, (pb * 16 + i) * 2)
                    px[tx * 8 + x, ty * 8 + y] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3, 255)
    return img


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    d = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(d))
    os.makedirs(out, exist_ok=True)
    maps = open(d + '-map-top-special.bin', 'rb').read()
    vram = open(d + '-vram.bin', 'rb').read()
    pal = open(d + '-palettes.bin', 'rb').read()
    cb = 2 * 0x4000
    for name, off in (('bg3-shadows', 0), ('bg1-leaves', 0x2000)):
        img = render(maps[off:off + 0x2000], pal, vram, cb)
        img.save(os.path.join(out, name + '-full.png'))
        bbox = img.getbbox()
        h = ((bbox[3] + 7) // 8) * 8 if bbox else 1024
        strip = Image.new('RGBA', (400, h), (0, 0, 0, 0))
        strip.paste(img.crop((0, 0, 240, h)), (80, 0))
        strip.save(os.path.join(out, name + '-strip.png'))
        print(name, 'content height', h)
    return 0


if __name__ == '__main__':
    sys.exit(main())
