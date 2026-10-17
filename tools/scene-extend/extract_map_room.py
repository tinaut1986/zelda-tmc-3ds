#!/usr/bin/env python3
"""A room drawn from the game's map layers, and the template to extend it for a 400-wide view.

    extract_map_room.py DUMP_PREFIX [OUTDIR] [--left N] [--right N]

For the rooms that are a scrolling map (houses, shrines, most interiors), not a fixed BG picture:
their BGs hold only a 32x32 window of the room, so extract_bg.py finds a few tiles. The dump's
-map-bottom-special.bin and -map-top-special.bin hold the whole room (128 tile entries a row; the
bottom map is the layer the game draws under, BG2 in the usual layout, the top map over it), and
-room-controls.bin its size. DUMP_PREFIX is a screen dump's files without their suffix
(debug/tmc-dump-NNNN, fetched with tmc-link.py dump and curl); it reads -vram.bin, -io-registers.bin,
-palettes.bin, -room-controls.bin, -map-bottom-special.bin and -map-top-special.bin. Writes to OUTDIR
(default: the dump's folder):

  room-ground.png    the bottom map's picture (WxH of the room)
  room-top.png       the top map's picture, transparent where it has no pixel
  room-picture.png   the two together: what the game's map layers show, no sprites, no HUD
  room-template.png  the picture in a transparent canvas, --left columns before it (default 80, the
                     GBA's 240 columns centred in 400) and --right after it (default the rest of 400):
                     what the generative AI fills

Process and prompt: docs/wide-scene-extension.md. The outputs are the game's art: keep them out
of the repository.
"""
import argparse
import os
import struct

from PIL import Image


def rgb(pal, i):
    c, = struct.unpack_from('<H', pal, i * 2)
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def render_map(special, vram, pal, cnt, width, height):
    """The map's entries (128 per row) as a picture, tiles from the BG's character block."""
    cb, c256 = ((cnt >> 2) & 3) * 0x4000, (cnt >> 7) & 1
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    px = img.load()
    for ty in range(height // 8):
        for tx in range(width // 8):
            e, = struct.unpack_from('<H', special, (ty * 128 + tx) * 2)
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
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('dump')
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--right', type=int)
    a = ap.parse_args()
    read = lambda s: open(a.dump + '-' + s, 'rb').read()
    vram, io, pal, rc = read('vram.bin'), read('io-registers.bin'), read('palettes.bin'), read('room-controls.bin')
    width, height = struct.unpack_from('<HH', rc, 0x1e)
    out = a.outdir or os.path.dirname(os.path.abspath(a.dump))
    os.makedirs(out, exist_ok=True)
    # The map whose control word matches BGnCNT's screen base is the one that BG draws.
    cnts = [struct.unpack_from('<H', io, 8 + 2 * i)[0] for i in range(4)]
    info = open(a.dump + '-info.txt').read()
    bottom = int(info.split('Map BG controls: bottom=')[1].split()[0], 16)
    top = int(info.split('top=')[1].split()[0], 16)
    ground = render_map(read('map-bottom-special.bin'), vram, pal, bottom, width, height)
    over = render_map(read('map-top-special.bin'), vram, pal, top, width, height)
    picture = ground.copy()
    picture.alpha_composite(over)
    ground.save(os.path.join(out, 'room-ground.png'))
    over.save(os.path.join(out, 'room-top.png'))
    picture.save(os.path.join(out, 'room-picture.png'))
    right = a.right if a.right is not None else max(400 - a.left - width, 0)
    template = Image.new('RGBA', (a.left + width + right, height), (0, 0, 0, 0))
    template.paste(picture, (a.left, 0))
    template.save(os.path.join(out, 'room-template.png'))
    print('room %dx%d, BG controls %s, map controls bottom=%04x top=%04x' % (width, height, ' '.join('%04x' % c for c in cnts), bottom, top))
    print('template %dx%d (left %d, right %d)' % (template.width, template.height, a.left, right))


if __name__ == '__main__':
    main()
