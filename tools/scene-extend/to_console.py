#!/usr/bin/env python3
"""A scene's extension picture as the game reads it from the SD card (docs/wide-scene-extension.md).

    to_console.py SCENE PICTURE.png [OUT.bin] [--ip IP]

SCENE      portal: the Minish portal cutscene, PICTURE 400x320, the original at x 80..335;
                   writes portal-cutscene-strips.bin (its 80 columns left and 64 right, 144x320)
           title:  the title screen's forest, PICTURE 400x240, the original at 80,40 (240x160);
                   writes title-forest.bin (the whole 400x240; the centre is not drawn)
PICTURE    the AI's extension, whole or with the original area transparent (as kept in
           platform/3ds/assets/wide-scenes/): the original is never read.
--ip IP    upload it into the data folder over the game's FTP server (port 5000).

The file is RGB565, little endian, row by row. Without it the game shows black there.
"""
import argparse
import ftplib
import io
import struct
import sys

from PIL import Image

SCENES = {
    # name: (picture size, columns kept (None: all), file name)
    'portal': ((400, 320), list(range(0, 80)) + list(range(336, 400)), 'portal-cutscene-strips.bin'),
    'title': ((400, 240), None, 'title-forest.bin'),
}


def convert(scene, path):
    size, columns, _ = SCENES[scene]
    im = Image.open(path).convert('RGB')
    if im.size != size:
        sys.exit('%s is %dx%d, the %s scene needs %dx%d' % (path, im.size[0], im.size[1], scene, *size))
    px = im.load()
    cols = columns if columns is not None else range(size[0])
    out = bytearray()
    for y in range(size[1]):
        for x in cols:
            r, g, b = px[x, y]
            out += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('scene', choices=sorted(SCENES))
    ap.add_argument('picture')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--ip')
    args = ap.parse_args()
    name = SCENES[args.scene][2]
    data = convert(args.scene, args.picture)
    out = args.out or name
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes' % (out, len(data)))
    if args.ip:
        ftp = ftplib.FTP()
        ftp.connect(args.ip, 5000, timeout=20)
        ftp.login()
        ftp.storbinary('STOR /3ds/Minish Cap 3DS/' + name, io.BytesIO(data))
        ftp.quit()
        print('uploaded to %s' % args.ip)


if __name__ == '__main__':
    main()
