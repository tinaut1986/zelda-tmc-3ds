#!/usr/bin/env python3
"""Decodes a scene recording (debug/tmc-scene-NNNN.bin, SITE SCENEREC; docs/debug-tools.md).

    decode.py REC.bin [OUTDIR] [--gif OUT.gif] [--first N] [--last N] [--scale N] [--crop X,Y,W,H]

OUTDIR (default: REC name without .bin) gets frame-NNNN.png. --gif also writes an animated GIF
at the rate the frames were taken (one presented frame in `every`, at 60 Hz); --first/--last pick
frames (0-based, inclusive), --crop a rectangle, --scale an integer nearest-neighbour upscale.
Needs python3 + Pillow.

Format: header u32 'SCN1', count, width (400), height (240), every; then per frame u32 frame
number and width*height RGB565 in the framebuffer's order: column x from the left, each column
from the bottom row up.
"""
import argparse
import os
import struct
import sys

from PIL import Image


def frames(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, count, w, h, every = struct.unpack_from('<5I', data, 0)
    if magic != 0x314E4353:
        sys.exit('%s is not a scene recording' % path)
    size = 4 + w * h * 2
    for i in range(count):
        off = 20 + i * size
        number, = struct.unpack_from('<I', data, off)
        raw = data[off + 4:off + size]
        # column-major, bottom-up RGB565 -> a w x h image
        img = Image.frombuffer('RGB', (h, w), raw, 'raw', 'BGR;16', 0, 1)
        img = img.transpose(Image.Transpose.ROTATE_90)
        yield number, img, every


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('rec')
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--gif')
    ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--last', type=int, default=-1)
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--crop')
    args = ap.parse_args()
    out = args.outdir or os.path.splitext(args.rec)[0]
    os.makedirs(out, exist_ok=True)
    crop = tuple(int(v) for v in args.crop.split(',')) if args.crop else None
    picked, every = [], 2
    for i, (number, img, every) in enumerate(frames(args.rec)):
        if i < args.first or (args.last >= 0 and i > args.last):
            continue
        if crop:
            img = img.crop((crop[0], crop[1], crop[0] + crop[2], crop[1] + crop[3]))
        if args.scale > 1:
            img = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        img.save(os.path.join(out, 'frame-%04d.png' % i))
        picked.append(img)
    print('%s: %d frames, one in %d at 60 Hz' % (out, len(picked), every))
    if args.gif and picked:
        picked[0].save(args.gif, save_all=True, append_images=picked[1:], loop=0,
                       duration=round(1000 * every / 60), optimize=False)
        print(args.gif)


if __name__ == '__main__':
    main()
