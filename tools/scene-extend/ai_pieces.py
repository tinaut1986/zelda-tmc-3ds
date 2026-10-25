#!/usr/bin/env python3
"""A layer's side strips made by a generative AI in pieces, each continuing the one before (docs/wide-scene-extension.md,
"AI pieces"). For the vertical Minish paths' layers and anything like them: the original 240 columns at x 80 of a 400
picture, the AI paints x 0-79 and 320-399.

    ai_pieces.py make LAYER.png N OUT.png [--prev RESULT.png] [--chunk 320] [--step 240] [--keep 80] [--opaque]
    ai_pieces.py fit TEMPLATE.png AI_ANSWER.jpg OUT.png --palette LAYER.png [--keep 80] [--band Y0 Y1] [--opaque]

make   the template for piece N (from 1): LAYER's rows (N-1)*step .. +chunk at x 80 with its transparency, the sides
       black (where the AI paints; on a layer with transparency black also means "nothing" in the answer, so the
       layer must not use pure black). From piece 2 on, the sides' first --keep rows come from the previous piece's
       result (--prev), so the AI continues them. The last piece is moved up to end at the layer's last row; --keep
       then grows to the real overlap, printed.
fit    the AI's answer (any size: a JPG upscaled 5-8 times is usual) brought back to TEMPLATE's size: the scale and
       offset that best match the template's original part are searched, every side pixel takes the median of its
       cell mapped to the nearest of LAYER's colours (near black: nothing, unless --opaque), the original part is put
       back as it was. With --keep N the sides' first N rows come from the template (the piece before) above a
       per-column seam through the rows where both agree, so the join has no straight cut. With --band Y0 Y1 (a
       join given back to be repainted) only the rows between two such seams, around the band, come from the answer.
       Prints how well the original part matched (opacity and colour), and the disagreeing pixels left on each seam.
--opaque  a layer without transparency (a ground): black is a colour to paint, never "nothing".

The pieces and answers are the game's art and the AI's: keep them in ~/Imágenes/<scene>/, never in git; only the
finished sides go to platform/3ds/assets/wide-scenes/ (the original's columns transparent).
"""
import argparse
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.array(Image.open(path).convert('RGBA')).astype(int)


def make(args):
    layer = load(args.layer)[:, :240]
    h = layer.shape[0]
    y0 = min((args.n - 1) * args.step, h - args.chunk)
    keep = args.keep if args.n > 1 else 0
    if args.n > 1:
        prev_y0 = min((args.n - 2) * args.step, h - args.chunk)
        keep = prev_y0 + args.chunk - y0
    out = np.zeros((args.chunk, 400, 4), np.uint8)
    out[..., 3] = 255
    out[:, 80:320] = layer[y0:y0 + args.chunk]
    if keep:
        if not args.prev:
            sys.exit('piece %d needs --prev (the result of piece %d)' % (args.n, args.n - 1))
        prev = load(args.prev)
        band = prev[args.chunk - keep:args.chunk]
        for x0, x1 in ((0, 80), (320, 400)):
            b = band[:, x0:x1].copy()
            b[b[..., 3] == 0] = (0, 0, 0, 255)
            out[:keep, x0:x1] = b
    Image.fromarray(out).save(args.out)
    print('%s: rows %d-%d of the layer; the sides\' first %d rows come from the piece before' %
          (args.out, y0, y0 + args.chunk - 1, keep))


def search(g, tmpl):
    """Coarse on every third pixel over a wide range (answers come squeezed, e.g. 5.64 x 5.60 with a 72-pixel margin),
    then fine around the best. The original part's transparent pixels are compared as the black the AI paints there."""
    H, W, _ = g.shape
    h, w, _ = tmpl.shape
    ref = tmpl[:, 80:320, :3].copy()
    ref[tmpl[:, 80:320, 3] == 0] = 0

    def scan(sxs, sys_, oxs, oys, sub):
        best = None
        for sx in sxs:
            for sy in sys_:
                for ox in oxs:
                    for oy in oys:
                        xs = (np.arange(w) * sx + sx / 2 + ox).astype(int)
                        ys = (np.arange(h) * sy + sy / 2 + oy).astype(int)
                        if xs.max() >= W or ys.max() >= H or xs.min() < 0 or ys.min() < 0:
                            continue
                        d = np.abs(g[ys[::sub]][:, xs[80:320:sub]] - ref[::sub, ::sub]).sum(2).mean()
                        if best is None or d < best[0]:
                            best = (d, sx, sy, ox, oy)
        return best

    _, sx, sy, ox, oy = scan(np.arange(W / w - 0.45, W / w + 0.451, 0.03), np.arange(H / h - 0.45, H / h + 0.451, 0.03),
                             range(-96, 97, 8), range(-96, 97, 8), 3)
    return scan(np.arange(sx - 0.03, sx + 0.031, 0.01), np.arange(sy - 0.03, sy + 0.031, 0.01),
                range(ox - 8, ox + 9, 2), range(oy - 8, oy + 9, 2), 1)


def seam(a, b, lo, hi, cols):
    """The cheapest per-column path (one row of change per column) through rows lo..hi-1 where a and b disagree."""
    cost = (a[lo:hi][:, cols] != b[lo:hi][:, cols]).any(2).astype(float)
    h, w = cost.shape
    D = np.full((h, w), 1e9)
    P = np.zeros((h, w), int)
    D[:, 0] = cost[:, 0]
    for x in range(1, w):
        for y in range(h):
            D[y, x], P[y, x] = min((D[yy, x - 1], yy) for yy in (y - 1, y, y + 1) if 0 <= yy < h)
            D[y, x] += cost[y, x]
    y = int(np.argmin(D[:, w - 1]))
    s = [0] * w
    for x in range(w - 1, -1, -1):
        s[x] = y + lo
        y = P[y, x]
    return s, int(sum(cost[s[x] - lo, x] for x in range(w)))


def fit(args):
    g = np.array(Image.open(args.answer).convert('RGB')).astype(int)
    H, W, _ = g.shape
    tmpl = load(args.template)
    h, w, _ = tmpl.shape
    d, sx, sy, ox, oy = search(g, tmpl)
    full = load(args.palette)
    pal = np.array(sorted(set(map(tuple, full[full[..., 3] > 0][:, :3]))))
    out = np.zeros((h, w, 4), int)
    for y in range(h):
        for x in list(range(80)) + list(range(320, w)):
            cx = min(W - 2, max(1, int(x * sx + sx / 2 + ox)))
            cy = min(H - 2, max(1, int(y * sy + sy / 2 + oy)))
            c = np.median(g[cy - 1:cy + 2, cx - 1:cx + 2].reshape(-1, 3), 0)
            if not args.opaque and c.sum() < 60:
                continue
            out[y, x, :3] = pal[((pal - c) ** 2).sum(1).argmin()]
            out[y, x, 3] = 255
    # How well the original part came back (the answer's own pixels there are never used).
    xs = (np.arange(w) * sx + sx / 2 + ox).astype(int)
    ys = (np.arange(h) * sy + sy / 2 + oy).astype(int)
    centre = g[ys][:, xs][:, 80:320]
    o = tmpl[:, 80:320]
    print('%dx%d, scale %.2f x %.2f, offset %+d %+d; original part: mean difference %.1f' % (W, H, sx, sy, ox, oy, d))
    out[:, 80:320] = o
    sides = np.r_[0:80, 320:w]
    t = tmpl.copy()
    if not args.opaque:
        t[(t[..., :3].sum(2) == 0)] = 0  # the template's black sides are "nothing"
    if args.band:
        y0, y1 = args.band
        top, ct = seam(out, t, max(0, y0 - 24), y0 + 1, sides)
        bot, cb = seam(out, t, y1, min(h, y1 + 25), sides)
        res = t.copy()
        for i, x in enumerate(sides):
            res[top[i]:bot[i], x] = out[top[i]:bot[i], x]
        print('band %d-%d: seams at rows %d-%d and %d-%d, disagreeing pixels %d and %d' %
              (y0, y1, min(top), max(top), min(bot), max(bot), ct, cb))
        out = res
    elif args.keep:
        s, c = seam(out, t, 8, args.keep, sides)
        for i, x in enumerate(sides):
            out[:s[i], x] = t[:s[i], x]
        print('overlap: seam at rows %d-%d, disagreeing pixels %d' % (min(s), max(s), c))
    Image.fromarray(out.astype(np.uint8)).save(args.out)
    print('%s written' % args.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('make')
    m.add_argument('layer')
    m.add_argument('n', type=int)
    m.add_argument('out')
    m.add_argument('--prev')
    m.add_argument('--chunk', type=int, default=320)
    m.add_argument('--step', type=int, default=240)
    m.add_argument('--keep', type=int, default=80)
    m.add_argument('--opaque', action='store_true')
    f = sub.add_parser('fit')
    f.add_argument('template')
    f.add_argument('answer')
    f.add_argument('out')
    f.add_argument('--palette', required=True)
    f.add_argument('--keep', type=int, default=0)
    f.add_argument('--band', type=int, nargs=2)
    f.add_argument('--opaque', action='store_true')
    args = ap.parse_args()
    make(args) if args.cmd == 'make' else fit(args)


if __name__ == '__main__':
    main()
