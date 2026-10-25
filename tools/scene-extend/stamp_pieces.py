#!/usr/bin/env python3
"""A layer made of a few repeated pieces, extended by completing each piece once (docs/wide-scene-extension.md).

    stamp_pieces.py LAYER.png OUT.png --piece CUT.png DONE.png X0 [--piece ...] [--left 80] [--width 400]

LAYER is the layer as the GBA shows it (240 wide, transparent where empty), placed at x = --left of a --width wide
picture. Each --piece names the piece as it is cut in LAYER (CUT: the template given to the AI, transparent, or magenta
ff00ff, where empty; black where the AI may paint), the AI's answer (DONE, the same size, the piece completed past the cut) and X0, the column of CUT that is
LAYER's column 0 (80 for a piece cut by the left edge, -160 for one cut by the right edge of a 240-wide layer).
Every place where the piece's part inside LAYER matches LAYER exactly, also moved sideways (a copy cut further), gets
DONE's pixels outside LAYER's columns. LAYER's own pixels are never changed. OUT is RGBA, transparent where empty.
"""
import argparse
import sys

from PIL import Image

KEY = (255, 0, 255)
EMPTY = (KEY, (0, 0, 0))  # pure black: where the AI was allowed to paint and did not


def opaque_mask(img):
    w, h = img.size
    px = img.load()
    return {(x, y): px[x, y] for y in range(h) for x in range(w) if px[x, y][3] and px[x, y][:3] not in EMPTY}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('layer')
    ap.add_argument('out')
    ap.add_argument('--piece', nargs=3, action='append', metavar=('CUT', 'DONE', 'X0'), required=True)
    ap.add_argument('--left', type=int, default=80)
    ap.add_argument('--width', type=int, default=400)
    args = ap.parse_args()
    layer = Image.open(args.layer).convert('RGBA')
    lw, lh = layer.size
    lpx = layer.load()
    out = Image.new('RGBA', (args.width, lh), (0, 0, 0, 0))
    opx = out.load()
    for cut_path, done_path, x0s in args.piece:
        cut = Image.open(cut_path).convert('RGBA')
        done = Image.open(done_path).convert('RGBA')
        if done.size != cut.size:
            sys.exit('%s is %dx%d, %s %dx%d' % (done_path, *done.size, cut_path, *cut.size))
        x0 = int(x0s)
        cw, ch = cut.size
        inside = opaque_mask(cut)  # the piece as the layer has it (only its columns inside the layer)
        dpx = done.load()
        found = 0
        for shift in range(-16, 17):
            # The piece's pixels that fall in the layer's columns with this sideways move.
            want = {(x - x0 + shift, y): c for (x, y), c in inside.items() if 0 <= x - x0 + shift < lw}
            if len(want) < len(inside) // 2:
                continue
            for top in range(-ch, lh):
                ok = True
                for (lx, y), c in want.items():
                    ly = y + top
                    if not 0 <= ly < lh:
                        continue  # past the layer's top or bottom: a copy cut there too
                    if lpx[lx, ly][3] == 0 or lpx[lx, ly][:3] != c[:3]:
                        ok = False
                        break
                inside_rows = sum(1 for (lx, y) in want if 0 <= y + top < lh)
                if not ok or inside_rows < len(want) // 2:
                    continue
                found += 1
                for y in range(ch):
                    for x in range(cw):
                        lx, ly = x - x0 + shift, y + top
                        if 0 <= lx < lw or not 0 <= ly < lh:
                            continue
                        ox = lx + args.left
                        p = dpx[x, y]
                        if 0 <= ox < args.width and p[3] and p[:3] not in EMPTY:
                            opx[ox, ly] = (p[0], p[1], p[2], 255)
                print('%s at layer x %+d, y %d' % (cut_path, shift, top))
        if not found:
            print('%s: not found in the layer' % cut_path)
    out.alpha_composite(layer, (args.left, 0))
    out.save(args.out)
    print('%s: %dx%d' % (args.out, *out.size))


if __name__ == '__main__':
    main()
