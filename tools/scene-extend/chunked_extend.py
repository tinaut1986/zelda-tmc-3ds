#!/usr/bin/env python3
"""A tall room's side strips generated in pieces, each one continuing the one before (docs/wide-scene-extension.md).

    chunked_extend.py make PICTURE.png DIR N [--chunk 320] [--overlap 80] [--left 80] [--width 400] [--key RRGGBB]
    chunked_extend.py stitch PICTURE.png DIR [--chunk 320] [--overlap 80] [--left 80] [--width 400] [--key RRGGBB]

PICTURE is the room's picture (room-ground.png from extract_map_room.py), placed at x = --left of a --width wide
canvas. A generative AI is given one piece at a time, `chunk` rows tall; the pieces start every chunk - overlap rows,
so each one after the first begins with the last `overlap` rows of the one before.

make N    writes DIR/chunk-N.png (N from 1): the original in the middle, black beside it, and for N > 1 the top
          `overlap` rows beside it taken from DIR/result-(N-1).png (the AI's answer for the piece before, which must
          be there), so the AI continues it. It prints the rows the piece covers and what the prompt has to say.
stitch    joins DIR/result-1.png ... into DIR/extended.png: piece N gives its rows past the overlap (piece 1 all of
          its rows), the original pasted back over the middle. Check it with check_extension.py.

--key RRGGBB   for a layer with transparent pixels (a parallax layer's ledges over nothing): the pieces show the
          transparent pixels and the areas to fill in that colour (pure magenta, ff00ff, for an AI to keep as the
          background) instead of black, and stitch turns it back into transparency in extended.png.

A result must have the piece's size; its original area and its overlap rows may differ a little (the AI repaints):
both are taken from the original and from the piece before, never from the result.
"""
import argparse
import os
import sys

from PIL import Image

BLACK = (0, 0, 0, 255)


def starts(height, chunk, overlap):
    step = chunk - overlap
    out, y = [], 0
    while True:
        out.append(y)
        if y + chunk >= height:
            return out
        y = min(y + step, height - chunk)


def load(path):
    if not os.path.exists(path):
        sys.exit('missing %s' % path)
    return Image.open(path).convert('RGBA')


def flatten(img, fill):
    # Transparent pixels in the fill colour (an AI answer is opaque; the original may not be).
    out = Image.new('RGBA', img.size, fill)
    out.alpha_composite(img)
    return out


def make(args, pic):
    ys = starts(pic.height, args.chunk, args.overlap)
    n = args.n
    if not 1 <= n <= len(ys):
        sys.exit('the picture makes %d pieces (1..%d)' % (len(ys), len(ys)))
    y0 = ys[n - 1]
    h = min(args.chunk, pic.height - y0)
    canvas = Image.new('RGBA', (args.width, h), args.fill)
    keep = 0
    if n > 1:
        prev = flatten(load(os.path.join(args.dir, 'result-%d.png' % (n - 1))), args.fill)
        py0 = ys[n - 2]
        if prev.size != (args.width, min(args.chunk, pic.height - py0)):
            sys.exit('result-%d.png is %dx%d, its piece was %dx%d' % (n - 1, *prev.size, args.width,
                                                                       min(args.chunk, pic.height - py0)))
        keep = py0 + prev.height - y0  # rows of this piece the one before already painted
        canvas.paste(prev.crop((0, y0 - py0, args.width, y0 - py0 + keep)), (0, 0))
    canvas.paste(pic.crop((0, y0, pic.width, y0 + h)), (args.left, 0))
    out = os.path.join(args.dir, 'chunk-%d.png' % n)
    canvas.save(out)
    right = args.left + pic.width
    print('%s: %dx%d, rows %d..%d of the room (piece %d of %d)' % (out, args.width, h, y0, y0 + h - 1, n, len(ys)))
    print('  original: x %d..%d, every row; to fill: the %s at x 0..%d and x %d..%d' %
          (args.left, right - 1, args.fill_name, args.left - 1, right, args.width - 1))
    if keep:
        print('  rows 0..%d at the sides are already painted (the end of piece %d): keep them, continue below them'
              % (keep - 1, n - 1))
    print('  save the answer as %s' % os.path.join(args.dir, 'result-%d.png' % n))


def stitch(args, pic):
    ys = starts(pic.height, args.chunk, args.overlap)
    out = Image.new('RGBA', (args.width, pic.height), args.fill)
    for n, y0 in enumerate(ys, 1):
        res = flatten(load(os.path.join(args.dir, 'result-%d.png' % n)), args.fill)
        h = min(args.chunk, pic.height - y0)
        if res.size != (args.width, h):
            sys.exit('result-%d.png is %dx%d, its piece was %dx%d' % (n, *res.size, args.width, h))
        skip = 0 if n == 1 else ys[n - 2] + min(args.chunk, pic.height - ys[n - 2]) - y0
        out.paste(res.crop((0, skip, args.width, h)), (0, y0 + skip))
    out.paste(pic, (args.left, 0))
    path = os.path.join(args.dir, 'extended.png')
    if args.key is None:
        out.convert('RGB').save(path)
    else:
        key = args.fill[:3]
        data = bytearray(out.tobytes())
        for i in range(0, len(data), 4):
            if tuple(data[i:i + 3]) == key:
                data[i:i + 4] = b'\0\0\0\0'
        out = Image.frombytes('RGBA', out.size, bytes(data))
        out.save(path)
    print('%s: %dx%d from %d pieces' % (path, args.width, pic.height, len(ys)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('command', choices=('make', 'stitch'))
    ap.add_argument('picture')
    ap.add_argument('dir')
    ap.add_argument('n', type=int, nargs='?')
    ap.add_argument('--chunk', type=int, default=320)
    ap.add_argument('--overlap', type=int, default=80)
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--width', type=int, default=400)
    ap.add_argument('--key', help='RRGGBB: transparency shown in this colour')
    args = ap.parse_args()
    if args.key is None:
        args.fill, args.fill_name = BLACK, 'black'
    else:
        k = int(args.key, 16)
        args.fill, args.fill_name = ((k >> 16) & 255, (k >> 8) & 255, k & 255, 255), '#' + args.key.lower()
    if args.overlap >= args.chunk:
        sys.exit('--overlap must be under --chunk')
    pic = flatten(load(args.picture), args.fill)
    if args.left + pic.width > args.width:
        sys.exit('the picture does not fit at x %d of %d' % (args.left, args.width))
    if args.command == 'make':
        if args.n is None:
            sys.exit('make needs N')
        make(args, pic)
    else:
        stitch(args, pic)


if __name__ == '__main__':
    main()
