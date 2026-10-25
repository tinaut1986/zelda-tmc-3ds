#!/usr/bin/env python3
"""A background that repeats one motif on a lattice, extended by code, no generative AI (docs/wide-scene-extension.md).

    extend_lattice.py PICTURE.png OUT.png [--left 80] [--top 40] [--width 400] [--height 240] [--vectors DX,DY DX,DY]
                      [--margin 4] [--complete X0,Y0,X1,Y1 ...] [--transparent]

PICTURE is the screen's background as the GBA shows it (extract_bg.py's bgN-picture.png), placed at --left, --top
of a --width x --height picture. Without --vectors the two shortest shifts under which the picture matches itself
(nearly: the game's map may break the pattern in places) are found and printed. Every pixel outside the original
takes the colour most of its copies inside the picture have (each copy a whole number of lattice steps away, at
least --margin pixels inside the edges), so the few places off the pattern are outvoted.

--complete X0,Y0,X1,Y1   a motif the original cuts at its left or right edge off the lattice (the pause menu's
          background has one at its left edge): the rectangle of the picture holding the cut part; its pixels that
          are not the background colour are matched against the rest of the picture and the part past the edge is
          copied from the whole motif found.
--transparent   leave the original's area transparent in OUT (as kept in platform/3ds/assets/wide-scenes/).
"""
import argparse
import sys
from collections import Counter

import numpy as np
from PIL import Image


def find_vectors(a):
    h, w = a.shape
    scores = []
    for dy in range(0, h // 2 + 1):
        for dx in range(-w // 2, w // 2 + 1):
            if dy == 0 and dx <= 0:
                continue
            p = a[0:h - dy, max(0, -dx):w - max(0, dx)]
            q = a[dy:h, max(0, dx):w + min(0, dx)]
            if p.size >= 2000:
                scores.append(((p == q).mean(), dx, dy))
    good = sorted((s for s in scores if s[0] > 0.995), key=lambda s: s[1] ** 2 + s[2] ** 2)
    if not good:
        sys.exit('no shift repeats the picture: not a lattice pattern')
    v1 = good[0][1:]
    for s in good[1:]:
        v2 = s[1:]
        if v1[0] * v2[1] - v1[1] * v2[0] != 0:
            print('lattice %s (%.4f) and %s (%.4f)' % (v1, good[0][0], v2, s[0]))
            return v1, v2
    sys.exit('only one direction repeats the picture')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('picture')
    ap.add_argument('out')
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--width', type=int, default=400)
    ap.add_argument('--height', type=int, default=240)
    ap.add_argument('--vectors', nargs=2)
    ap.add_argument('--margin', type=int, default=4)
    ap.add_argument('--complete', nargs='*', default=[])
    ap.add_argument('--transparent', action='store_true')
    args = ap.parse_args()
    pic = np.array(Image.open(args.picture).convert('RGB'))
    h, w, _ = pic.shape
    key = (pic[..., 0].astype(np.int32) << 16) | (pic[..., 1].astype(np.int32) << 8) | pic[..., 2]
    if args.vectors:
        v1, v2 = (tuple(int(n) for n in v.split(',')) for v in args.vectors)
    else:
        v1, v2 = find_vectors(key)
    L, T, m = args.left, args.top, args.margin
    out = np.zeros((args.height, args.width, 3), np.uint8)
    reach = max(args.width, args.height) // max(1, min(abs(v1[0]) + abs(v1[1]), abs(v2[0]) + abs(v2[1]))) + 4
    for y in range(args.height):
        for x in range(args.width):
            px, py = x - L, y - T
            if 0 <= px < w and 0 <= py < h:
                out[y, x] = pic[py, px]
                continue
            votes = Counter()
            for i in range(-reach, reach + 1):
                for j in range(-reach, reach + 1):
                    qx, qy = px + i * v1[0] + j * v2[0], py + i * v1[1] + j * v2[1]
                    if m <= qx < w - m and m <= qy < h - m:
                        votes[key[qy, qx]] += 1
            if not votes:
                sys.exit('no copy of %d,%d inside the picture: lower --margin' % (x, y))
            k = votes.most_common(1)[0][0]
            out[y, x] = ((k >> 16) & 255, (k >> 8) & 255, k & 255)
    background = Counter(key.ravel().tolist()).most_common(1)[0][0]
    for box in args.complete:
        x0, y0, x1, y1 = (int(n) for n in box.split(','))
        cut = [(x, y) for y in range(y0, y1) for x in range(x0, x1) if key[y, x] != background]
        bw = x1 - x0
        past = range(-bw, 0) if x0 < w // 2 else range(w, w + bw)  # the columns past the edge it continues into
        found = None
        for sy in range(-y0, h - y1 + 1):
            for sx in range(-x0 + bw, w - x1 - bw + 1):  # the copy and its continuation inside the picture
                if (sx or sy) and all(key[y + sy, x + sx] == key[y, x] for x, y in cut):
                    found = (sx, sy)
                    break
            if found:
                break
        if not found:
            sys.exit('no whole copy of the motif cut at %s' % box)
        sx, sy = found
        print('motif cut at %s completed from the copy %+d,%+d away' % (box, sx, sy))
        for y in range(y0, y1):
            for x in past:
                if key[y + sy, x + sx] != background:
                    out[y + T, x + L] = pic[y + sy, x + sx]
    img = Image.fromarray(out).convert('RGBA')
    if args.transparent:
        img.paste((0, 0, 0, 0), (L, T, L + w, T + h))
    img.save(args.out)
    print('%s: %dx%d' % (args.out, args.width, args.height))


if __name__ == '__main__':
    main()
