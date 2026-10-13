#!/usr/bin/env python3
"""Side strips for the Minish portal cutscene in Full View (PLAN P4.3).

The cutscene's picture is 256x320; in the 400x240 view it is centred at x=80, so the view
needs 80 columns left of it and 64 right of it. Give this a 400x320 picture with the original
at x=80..335 (the template made from a screen dump) and it writes those two strips as the
game reads them: 144x320 RGB565, little endian, left strip first. Copy the result to
sdmc:/3ds/Minish Cap 3DS/portal-cutscene-strips.bin. Without the file the strips are black.

    make_strips.py PICTURE.png [OUT.bin] [--ip IP]   (--ip: upload over the game's FTP server)
"""
import argparse
import ftplib
import io
import struct
import sys

from PIL import Image

LEFT, RIGHT, PICTURE_W, H = 80, 64, 256, 320


def strips(path):
    im = Image.open(path).convert('RGB')
    if im.size != (LEFT + PICTURE_W + RIGHT, H):
        sys.exit('%s is %dx%d, expected %dx%d' % (path, im.size[0], im.size[1], LEFT + PICTURE_W + RIGHT, H))
    px = im.load()
    out = bytearray()
    for y in range(H):
        for x in list(range(LEFT)) + list(range(LEFT + PICTURE_W, LEFT + PICTURE_W + RIGHT)):
            r, g, b = px[x, y]
            out += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('picture')
    ap.add_argument('out', nargs='?', default='portal-cutscene-strips.bin')
    ap.add_argument('--ip', help="the console's address: upload over the game's FTP server (port 5000)")
    args = ap.parse_args()
    data = strips(args.picture)
    with open(args.out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes' % (args.out, len(data)))
    if args.ip:
        ftp = ftplib.FTP()
        ftp.connect(args.ip, 5000, timeout=20)
        ftp.login()
        ftp.storbinary('STOR /3ds/Minish Cap 3DS/portal-cutscene-strips.bin', io.BytesIO(data))
        ftp.quit()
        print('uploaded to %s' % args.ip)


if __name__ == '__main__':
    main()
