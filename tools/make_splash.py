#!/usr/bin/env python3
"""Makes platform/3ds/romfs/splash.rgb565 (400x240, RGB565 little endian, what
Platform3DS_ShowSplash draws on the top screen) from platform/3ds/assets/splash.png.

The logo is cut to its own bounds (the PNG has uneven transparent margins), fitted into the
screen with a margin and centred, over black. The margin is also what the 3D splash shifts into
(a few pixels each way), so nothing of the logo is cut.

    tools/make_splash.py [--margin 14]
"""
import argparse
import os
import struct

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "platform", "3ds", "assets", "splash.png")
DST = os.path.join(ROOT, "platform", "3ds", "romfs", "splash.rgb565")
W, H = 400, 240


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--margin", type=int, default=14)
    args = ap.parse_args()
    src = Image.open(SRC).convert("RGBA")
    box = src.split()[3].point(lambda p: 255 if p > 8 else 0).getbbox()
    logo = src.crop(box)
    fit = min((W - 2 * args.margin) / logo.width, (H - 2 * args.margin) / logo.height)
    size = (round(logo.width * fit), round(logo.height * fit))
    logo = logo.resize(size, Image.LANCZOS)
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    canvas.alpha_composite(logo, ((W - size[0]) // 2, (H - size[1]) // 2))
    px = canvas.convert("RGB").load()
    out = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    with open(DST, "wb") as f:
        f.write(out)
    print("logo %dx%d from %s, placed at %d,%d in %dx%d" % (size[0], size[1], box, (W - size[0]) // 2, (H - size[1]) // 2, W, H))


if __name__ == "__main__":
    main()
