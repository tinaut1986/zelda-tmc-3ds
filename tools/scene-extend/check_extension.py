#!/usr/bin/env python3
"""Checks a generated extension of a scene's picture before it goes to the console.

    check_extension.py EXTENDED.png PICTURE.png [--left N] [--top N]

EXTENDED is the AI's result (the template filled), PICTURE the original (bgN-picture.png from
extract_bg.py), placed at x = --left (default 80), y = --top (default 0). It reports: the size; how many pixels of the
original area changed (must be 0: the original is drawn by the game, the extension only beside
it); and how many colours of the new parts are not in the original's palette (should be 0: an
off-palette colour shows as a seam). Exit status 1 when the first two fail.
"""
import argparse
import sys

from PIL import Image


def pixels(img):
    data = img.tobytes()
    return [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('extended')
    ap.add_argument('picture')
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--top', type=int, default=0)
    ap.add_argument('--width', type=int)
    ap.add_argument('--height', type=int)
    args = ap.parse_args()
    ext = Image.open(args.extended).convert('RGB')
    pic = Image.open(args.picture).convert('RGB')
    l, t = args.left, args.top
    if args.width is not None:
        w = args.width
    elif ext.width == 400 and pic.width == 256 and l == 80:
        # 240-wide corridor centred in 400: original tilemap is 256, but columns 240..255 are offscreen empty tiles
        right_16 = pic.crop((240, 0, 256, pic.height))
        non_zero = sum(1 for p in pixels(right_16) if p != (0, 0, 0))
        w = 240 if non_zero < 500 else 256
    else:
        w = pic.width
    h = args.height if args.height is not None else pic.height
    print('extended %dx%d, picture %dx%d at %d,%d' % (ext.width, ext.height, w, h, l, t))
    if ext.height < t + h or ext.width < l + w:
        print('FAIL: the extension must hold the picture at %d,%d' % (l, t))
        sys.exit(1)
    centre = ext.crop((l, t, l + w, t + h))
    pic_centre = pic.crop((0, 0, w, h))
    changed = sum(1 for a, b in zip(pixels(centre), pixels(pic_centre)) if a != b)
    palette = set(pixels(pic))
    everything = pixels(ext)
    inside = {(x, y) for y in range(t, t + h) for x in range(l, l + w)}
    sides = [c for i, c in enumerate(everything) if (i % ext.width, i // ext.width) not in inside]
    off = {c for c in sides if c not in palette}
    print('original area: %d of %d pixels changed' % (changed, w * h))
    print('new parts: %d colours, %d of them not in the original (%d pixels)' %
          (len(set(sides)), len(off), sum(1 for c in sides if c in off)))
    if changed:
        print('FAIL: the original area must be untouched (paste the original back over it)')
        sys.exit(1)
    print('OK' if not off else 'WARN: off-palette colours; map them to the nearest palette colour')


if __name__ == '__main__':
    main()
