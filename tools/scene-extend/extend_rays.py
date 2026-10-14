#!/usr/bin/env python3
"""The title's light rays (BG0) extended over the 400x240 view by copying the rays themselves.

    extend_rays.py DUMP_PREFIX [OUTDIR] [--bg N] [--left 80] [--top 40] [--forest PNG ...]

DUMP_PREFIX is a screen dump of the settled title (tmc-link.py dump, files fetched by name as in
docs/wide-scene-extension.md). Works on palette indices, never on colours, so the game can keep
rotating the palette (UpdateLightRays) and changing the blend over the result.

1. Reads BG N (default 0) as 4-bit indices, from the map's top-left corner to its last row with
   content (256 wide: the map wraps there).
2. Finds each ray (8-connected component) and measures its slope from its left and right edges
   and from how well each pixel matches its diagonal neighbour. Every ray must have the same
   slope, one pixel across per pixel down; the script stops otherwise and names the odd one.
3. Each diagonal of a ray that the picture cuts (at its top row or its last column) is continued
   up the slope to the view's edge by repeating a stretch of the ray's upper half on that same
   diagonal; one stretch length per ray, so the copies stay aligned across the ray. The original
   is placed at (--left, --top) and none of its pixels is touched; only cells outside it are
   written. Rays whole inside the picture, and the frayed lower ends, are left as they are.

Writes to OUTDIR (default: the dump's folder):

  rays-indices.png    400x240 paletted: the picture and its extension, 0 transparent
  rays-added.png      the same with only the added pixels (the original area transparent)
  rays-preview.png    the indices drawn with the dump's palette on black, 3x
  rays-on-forest.png  (--forest) blended over the forest as the title does (BLDALPHA 9/9), 3x;
                      --forest takes the extended forest's PNGs, drawn in order (e.g. the
                      generated parts, then the game's picture with --forest-at 80,40)

The outputs hold the game's art: keep them out of the repository.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

VIEW_W, VIEW_H = 400, 240


def read_bg(prefix, bg):
    vram = open(prefix + '-vram.bin', 'rb').read()
    io = open(prefix + '-io-registers.bin', 'rb').read()
    pal = open(prefix + '-palettes.bin', 'rb').read()
    cnt, = struct.unpack_from('<H', io, 8 + 2 * bg)
    if cnt & 0x80:
        sys.exit('BG%d is 256-colour; this works on 4-bit BGs' % bg)
    if cnt >> 14:
        sys.exit('BG%d is not 256x256 (BGCNT %04x)' % (bg, cnt))
    cb, sb = ((cnt >> 2) & 3) * 0x4000, ((cnt >> 8) & 31) * 0x800
    idx = np.zeros((256, 256), np.uint8)
    banks = set()
    for ty in range(32):
        for tx in range(32):
            e, = struct.unpack_from('<H', vram, sb + (ty * 32 + tx) * 2)
            t, hf, vf, pb = e & 0x3ff, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            for y in range(8):
                for x in range(8):
                    sx, sy = (7 - x if hf else x), (7 - y if vf else y)
                    b = vram[cb + t * 32 + sy * 4 + sx // 2]
                    i = (b >> (4 * (sx & 1))) & 15
                    idx[ty * 8 + y, tx * 8 + x] = i
                    if i:
                        banks.add(pb)
    if len(banks) != 1:
        sys.exit('BG%d uses palette banks %s; one is expected' % (bg, sorted(banks)))
    bank = banks.pop()
    rows = np.nonzero(idx.any(axis=1))[0]
    idx = idx[:rows.max() + 1]
    colours = []
    for i in range(16):
        c, = struct.unpack_from('<H', pal, (bank * 16 + i) * 2)
        colours.append(((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3))
    return idx, bank, colours


def measure(idx):
    lab, n = ndimage.label(idx > 0, structure=np.ones((3, 3)))
    rays = []
    for k in range(1, n + 1):
        ys, xs = np.nonzero(lab == k)
        if len(ys) < 20:
            continue  # a speck: nothing to measure
        rows = np.unique(ys)
        left = [xs[ys == r].min() for r in rows]
        right = [xs[ys == r].max() for r in rows]
        slope_l, slope_r = np.polyfit(rows, left, 1)[0], np.polyfit(rows, right, 1)[0]
        # how many pixels repeat one row down and one column left, for each candidate step
        match = {}
        for step in (-2, -1, 0, 1, 2):
            tot = eq = 0
            for y, x in zip(ys, xs):
                y2, x2 = y + 1, x + step
                if y2 < idx.shape[0] and 0 <= x2 < idx.shape[1] and lab[y2, x2] == k:
                    tot += 1
                    eq += idx[y, x] == idx[y2, x2]
            match[step] = eq / tot if tot else 0
        best = max(match, key=match.get)
        rays.append(dict(id=k, n=len(ys), x=(xs.min(), xs.max()), y=(ys.min(), ys.max()),
                         slope_l=slope_l, slope_r=slope_r, step=best, match=match[best]))
    return lab, rays


def extend(idx, lab, rays, left, top):
    h, w = idx.shape
    out = np.zeros((VIEW_H, VIEW_W), np.uint8)
    out[top:top + h, left:left + w] = idx
    added = np.zeros_like(out)
    inside = np.zeros((VIEW_H, VIEW_W), bool)
    inside[top:top + h, left:left + w] = True
    report = []
    for ray in rays:
        k = ray['id']
        mask = lab == k
        # a diagonal: x + y constant (one column left per row down); v = y along it
        diags = {}
        for y, x in zip(*np.nonzero(mask)):
            diags.setdefault(x + y, []).append(y)
        cut = {}
        for u, vs in diags.items():
            v0 = min(vs)
            x0 = u - v0
            if v0 != 0 and x0 != w - 1:
                continue  # this diagonal starts inside the picture: the ray's own end
            run = 0
            while v0 + run < h and 0 <= u - v0 - run < w and mask[v0 + run, u - v0 - run]:
                run += 1
            cut[u] = (v0, run)
        if not cut:
            continue
        # one stretch length for the whole ray: half its shortest full-length cut diagonal
        runs = sorted(r for _, r in cut.values())
        length = max(1, runs[len(runs) // 2] // 2)
        written = 0
        for u, (v0, run) in cut.items():
            seg = min(length, run)
            src = [idx[v0 + t, u - v0 - t] for t in range(seg)]
            v = v0 - 1
            t = seg - 1
            while True:
                vy, vx = v + top, u - v + left
                if vy < 0 or vx >= VIEW_W:
                    break
                if 0 <= vx and not inside[vy, vx]:
                    out[vy, vx] = src[t]
                    added[vy, vx] = src[t]
                    written += 1
                v -= 1
                t = (t - 1) % seg
        report.append('ray %d: %d diagonals cut, stretch %d px, %d pixels added' %
                      (k, len(cut), length, written))
    return out, added, report


def profile(idx, lab, k):
    """Ray k as {diagonal: (bottom v, function v -> index)}, continued upward without end where
    the picture cuts it, as extend() continues it."""
    h, w = idx.shape
    mask = lab == k
    diags = {}
    for y, x in zip(*np.nonzero(mask)):
        diags.setdefault(x + y, []).append(y)
    runs = {}
    for u, vs in diags.items():
        v0 = min(vs)
        if v0 == 0 or u - v0 == w - 1:
            run = 0
            while v0 + run < h and 0 <= u - v0 - run < w and mask[v0 + run, u - v0 - run]:
                run += 1
            runs[u] = run
    ordered = sorted(runs.values())
    length = max(1, ordered[len(ordered) // 2] // 2) if ordered else 1
    out = {}
    for u, vs in diags.items():
        v0, v1 = min(vs), max(vs)
        def value(v, u=u, v0=v0):
            if v >= v0:
                return idx[v, u - v] if mask[v, u - v] else 0
            if u not in runs:
                return 0
            seg = min(length, runs[u])
            return idx[v0 + (v - v0) % seg, u - v0 - (v - v0) % seg]
        out[u] = (v1, value)
    return out


def template(prof):
    """A ray profile as arrays (y, x, index) relative to the picture, its continuation up to 400
    rows above its lower end."""
    ys, xs, vals = [], [], []
    for u, (v1, value) in prof.items():
        for v in range(v1, v1 - 400, -1):
            i = value(v)
            if i:
                ys.append(v)
                xs.append(u - v)
                vals.append(i)
    return np.array(ys), np.array(xs), np.array(vals, np.uint8)


def place(tpl, dx, dy, left, top):
    """The view pixels of a copy of a ray moved by (dx, dy): (y, x, index) arrays. The copy runs
    up the slope until it leaves the view, so clipping to the view is all there is to do."""
    ys, xs, vals = tpl
    y, x = ys + top + dy, xs + left + dx
    keep = (y >= 0) & (y < VIEW_H) & (x >= 0) & (x < VIEW_W)
    return y[keep], x[keep], vals[keep]


def paletted(a, colours):
    im = Image.fromarray(a, 'P')
    flat = [c for rgb in colours for c in rgb]
    im.putpalette(flat)
    im.info['transparency'] = 0
    return im


def render(a, colours, bg=None):
    rgb = np.zeros(a.shape + (3,), np.uint8) if bg is None else bg.copy()
    lut = np.array(colours, np.uint8)
    on = a > 0
    if bg is None:
        rgb[on] = lut[a[on]]
    else:  # GBA blend, BLDALPHA 9/9: min(31, (top*9 + under*9) / 16) per 5-bit channel
        t = (lut[a[on]].astype(int) >> 3)
        b = (bg[on].astype(int) >> 3)
        rgb[on] = (np.minimum(31, (t * 9 + b * 9) >> 4) << 3).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB')


def suggest(idx, lab, rays, out, inside, left, top, gap=8, partial=False):
    """Places for a copy of each extended ray: `gap` pixels from every ray, off the original's
    area, its lower end in the original's rows (where the original's ends are) and at least half
    of it in the view; the most visible first. partial: the lower end may be past the view's left
    edge, and a quarter of the ray in the view is enough."""
    taken = ndimage.binary_dilation(out > 0, iterations=gap) | inside
    room = ndimage.distance_transform_cdt(out == 0, metric='chessboard')
    lowest = top + idx.shape[0] - 1
    found = []
    for r in rays:
        if r['y'][0] != 0 and r['x'][1] != idx.shape[1] - 1:
            continue  # only rays that reach the view's edge
        tpl = template(profile(idx, lab, r['id']))
        for dy in range(-160, 161, 2):
            end = tpl[0].max() + top + dy
            if not top <= end <= lowest:
                continue
            for dx in range(-400, 400, 2):
                if not partial and (tpl[1][tpl[0] == tpl[0].max()] + left + dx).min() < 0:
                    continue  # the lower end leaves the view
                y, x, _ = place(tpl, dx, dy, left, top)
                if len(y) < r['n'] // (4 if partial else 2) or taken[y, x].any():
                    continue
                found.append((len(y), room[y, x].min(), r['id'], dx, dy, int(x.mean())))
    found.sort(reverse=True)
    for side in ('left', 'right'):
        print('%s gap:' % side)
        rows = [f for f in found if (f[5] < VIEW_W // 2) == (side == 'left')][:8]
        for n, score, k, dx, dy, _ in rows:
            print('  --add %d:%d,%d   %d px, %d px from the nearest ray' % (k, dx, dy, n, score))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('prefix')
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--bg', type=int, default=0)
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--add', action='append', default=[], metavar='RAY:DX,DY',
                    help='a copy of ray RAY (numbered as printed) moved by DX,DY into a gap')
    ap.add_argument('--suggest', action='store_true', help='list places for new rays in the gaps')
    ap.add_argument('--partial', action='store_true',
                    help='with --suggest: rays whose lower end is past the left edge too')
    ap.add_argument('--forest', nargs='*', default=[])
    ap.add_argument('--forest-at', default='0,0', help='where the last --forest PNG goes')
    args = ap.parse_args()
    outdir = args.outdir or os.path.dirname(os.path.abspath(args.prefix))

    idx, bank, colours = read_bg(args.prefix, args.bg)
    print('BG%d: %dx%d, palette bank %d' % (args.bg, idx.shape[1], idx.shape[0], bank))
    lab, rays = measure(idx)
    for r in rays:
        print('ray %d: %4d px, x %3d..%3d, y %3d..%3d, edge slopes %+.2f %+.2f, '
              'pixel one row down at x%+d matches %.0f%%' %
              (r['id'], r['n'], *r['x'], *r['y'], r['slope_l'], r['slope_r'], r['step'], 100 * r['match']))
    odd = [r for r in rays if r['step'] != -1]
    if odd:
        sys.exit('rays with another slope: %s (expected one column left per row down)' %
                 ', '.join(str(r['id']) for r in odd))
    print('all %d rays: one column left per row down (45 degrees, light from the upper right)' % len(rays))

    out, added, report = extend(idx, lab, rays, args.left, args.top)
    print('\n'.join(report))
    inside = np.zeros(out.shape, bool)
    inside[args.top:args.top + idx.shape[0], args.left:args.left + idx.shape[1]] = True
    if args.suggest:
        pass
    for spec in args.add:
        k, d = spec.split(':')
        dx, dy = (int(v) for v in d.split(','))
        y, x, vals = place(template(profile(idx, lab, int(k))), dx, dy, args.left, args.top)
        bad = (inside[y, x] | (out[y, x] > 0)).sum()
        if bad:
            sys.exit('--add %s: %d pixels fall on the original or another ray' % (spec, bad))
        out[y, x] = added[y, x] = vals
        print('new ray %s: %d pixels' % (spec, len(y)))
    if args.suggest:  # around the rays already added
        suggest(idx, lab, rays, out, inside, args.left, args.top, partial=args.partial)
    paletted(out, colours).save(os.path.join(outdir, 'rays-indices.png'))
    paletted(added, colours).save(os.path.join(outdir, 'rays-added.png'))
    big = lambda im: im.resize((im.width * 3, im.height * 3), Image.NEAREST)
    big(render(out, colours)).save(os.path.join(outdir, 'rays-preview.png'))
    if args.forest:
        canvas = Image.new('RGB', (VIEW_W, VIEW_H))
        for i, path in enumerate(args.forest):
            im = Image.open(path).convert('RGBA')
            at = tuple(int(v) for v in args.forest_at.split(',')) if i == len(args.forest) - 1 else (0, 0)
            canvas.paste(im, at, im)
        big(render(out, colours, np.array(canvas))).save(os.path.join(outdir, 'rays-on-forest.png'))
    print('written to', outdir)


if __name__ == '__main__':
    main()
