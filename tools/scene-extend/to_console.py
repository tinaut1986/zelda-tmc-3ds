#!/usr/bin/env python3
"""A scene's extension picture as the game reads it from the SD card (docs/wide-scene-extension.md).

    to_console.py SCENE PICTURE.png [OUT.bin] [--ip IP] [--raw | --lz77]

SCENE      portal: the Minish portal cutscene, PICTURE 400x320, the original at x 80..335;
                   writes portal-cutscene-strips.bin (its 80 columns left and 64 right, 144x320)
           portal-rock: the rock's portal cutscene, PICTURE 400x320, the original at x 80..319;
                   writes portal-rock-strips.bin (its 80 columns left and 80 right, 160x320)
           crenel-ground, crenel-ledges, crenel-shadows: the Crenel paths' shaft (area 0x12 room 00), the room at
                   x 80 of a 400-wide PICTURE: its ground (BG2, 400x352: the top and one turn of its 320-row
                   repeat), its rock ledges (BG1, 400x960) and their shadows (BG3, 400x960); writes
                   crenel-paths-{ground,ledges,shadows}.bin, only the 80 columns each side of the room (160 wide),
                   0 where empty
           minish-leaves, minish-shadows: the Minish Paths' grass corridor (area 0x11, the vertical one), the 240-wide
                   layer at x 80 of a 400x1024 PICTURE: its leaves (BG1) and their shadows (BG3); writes
                   minish-paths-{leaves,shadows}.bin, the 80 columns each side (160 wide), 0 where empty
           title:  the title screen's forest, PICTURE 400x240, the original at 80,40 (240x160);
                   writes title-forest.bin (the whole 400x240; the centre is not drawn)
           pause:  the pause menu's background (BG3), PICTURE 400x240, the original at 80,40;
                   writes pause-background.bin (as title)
           fileselect: the file select screen's cave (BG3), PICTURE 400x240, the original at
                   80,40; needs --palettes. Writes fileselect-cave.bin: palette indices, not
                   colours, so the game's palette cycling (BG palettes 11-14) makes it shimmer
                   like the original: one byte a pixel, (palette - 11) * 16 + index, 0 where the
                   game's own picture is. Each pixel takes a palette holding its exact colour,
                   the one most of its 8x8 block uses when it can (the blocks then shimmer as one)
PICTURE    the AI's extension, whole or with the original area transparent (as kept in
           platform/3ds/assets/wide-scenes/): the original is never read.
--palettes DUMP-palettes.bin   (fileselect) the palettes of a screen dump of the file select
           screen, to map the picture's colours to palette indices
--ip IP    upload it into the data folder over the game's FTP server (port 5000).
--raw      write the picture as it is; by default it is a zlib stream (level 9). The game reads either
           (ScenePictures_ReadFile), and the GBA BIOS's LZ77 too:
--lz77     compress in the GBA BIOS's LZ77 format instead (0x10 and the size, then flag bytes and literals or
           back-copies; about 2.7 times zlib's size), should the build ever lose zlib.

The portal and title pictures are RGB565, little endian, row by row (then compressed with zlib, unless --raw). Without one the game shows
black there (the file select: the plain bars).
"""
import argparse
import ftplib
import io
import struct
import sys
import zlib

from PIL import Image

SCENES = {
    # name: (picture size, columns kept (None: all), file name)
    'portal': ((400, 320), list(range(0, 80)) + list(range(336, 400)), 'portal-cutscene-strips.bin'),
    'portal-rock': ((400, 320), list(range(0, 80)) + list(range(320, 400)), 'portal-rock-strips.bin'),
    'crenel-ground': ((400, 352), list(range(0, 80)) + list(range(320, 400)), 'crenel-paths-ground.bin'),
    'crenel-ledges': ((400, 960), list(range(0, 80)) + list(range(320, 400)), 'crenel-paths-ledges.bin'),
    'crenel-shadows': ((400, 960), list(range(0, 80)) + list(range(320, 400)), 'crenel-paths-shadows.bin'),
    'minish-leaves': ((400, 1024), list(range(0, 80)) + list(range(320, 400)), 'minish-paths-leaves.bin'),
    'minish-shadows': ((400, 1024), list(range(0, 80)) + list(range(320, 400)), 'minish-paths-shadows.bin'),
    'title': ((400, 240), None, 'title-forest.bin'),
    'fileselect': ((400, 240), None, 'fileselect-cave.bin'),
    'pause': ((400, 240), None, 'pause-background.bin'),
}
FILESELECT_BANKS = range(11, 15)  # the BG palettes sub_0805066C cycles (src/fileselect.c)
ORIGINAL = (80, 40, 240, 160)       # the game's own picture in the 400x240 view


def convert_indexed(path, palettes):
    pal = open(palettes, 'rb').read()
    banks = {}
    for b in FILESELECT_BANKS:
        for i in range(1, 16):  # index 0 is transparent
            c, = struct.unpack_from('<H', pal, (b * 16 + i) * 2)
            rgb = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
            banks.setdefault(rgb, []).append((b, i))
    im = Image.open(path).convert('RGBA')
    if im.size != (400, 240):
        sys.exit('%s is %dx%d, the fileselect scene needs 400x240' % (path, *im.size))
    px = im.load()
    ox, oy, ow, oh = ORIGINAL
    out = bytearray(400 * 240)
    missing = 0
    for ty in range(0, 240, 8):
        for tx in range(0, 400, 8):
            cells = [(x, y) for y in range(ty, ty + 8) for x in range(tx, tx + 8)
                     if not (ox <= x < ox + ow and oy <= y < oy + oh) and px[x, y][3]]
            votes = {}
            for x, y in cells:
                for b, _ in banks.get(px[x, y][:3], []):
                    votes[b] = votes.get(b, 0) + 1
            order = sorted(votes, key=lambda b: -votes[b])
            for x, y in cells:
                rgb = px[x, y][:3]
                found = banks.get(rgb)
                if not found:  # not one of the palettes' colours: the nearest in any of them
                    missing += 1
                    rgb = min(banks, key=lambda c: sum((a - b) ** 2 for a, b in zip(c, rgb)))
                    found = banks[rgb]
                bank, index = next((f for b in order for f in found if f[0] == b), found[0])
                out[y * 400 + x] = (bank - 11) * 16 + index
    if missing:
        print('%d pixels not in the palettes: mapped to the nearest colour' % missing)
    return bytes(out)


def lz77(data):
    """The GBA BIOS's LZ77 (LZ77UnCompWram): copies of 3-18 bytes from up to 4096 back, padded to 4 bytes."""
    out = bytearray([0x10, len(data) & 255, (len(data) >> 8) & 255, (len(data) >> 16) & 255])
    last = {}  # the latest positions of each 3-byte sequence, newest first
    i, n = 0, len(data)
    while i < n:
        flag_at = len(out)
        out.append(0)
        for bit in range(8):
            if i >= n:
                break
            best_len = best_back = 0
            for j in last.get(data[i:i + 3], ()):
                if i - j > 4096:
                    break
                length = 0
                while length < 18 and i + length < n and data[j + length] == data[i + length]:
                    length += 1
                if length > best_len:
                    best_len, best_back = length, i - j
                    if length == 18:
                        break
            step = best_len if best_len >= 3 else 1
            if best_len >= 3:
                out[flag_at] |= 0x80 >> bit
                d = best_back - 1
                out += bytes([((best_len - 3) << 4) | (d >> 8), d & 255])
            else:
                out.append(data[i])
            for k in range(i, i + step):
                if k + 3 <= n:
                    chain = last.setdefault(data[k:k + 3], [])
                    chain.insert(0, k)
                    del chain[64:]
            i += step
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def unlz77(data):
    size = data[1] | data[2] << 8 | data[3] << 16
    out = bytearray()
    i = 4
    while len(out) < size:
        flags = data[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                length, back = (data[i] >> 4) + 3, ((data[i] & 15) << 8 | data[i + 1]) + 1
                i += 2
                for _ in range(length):
                    out.append(out[-back])
            else:
                out.append(data[i])
                i += 1
    return bytes(out[:size])


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
    ap.add_argument('--palettes')
    ap.add_argument('--raw', action='store_true')
    ap.add_argument('--lz77', action='store_true')
    args = ap.parse_args()
    name = SCENES[args.scene][2]
    if args.scene == 'fileselect':
        if not args.palettes:
            sys.exit('fileselect needs --palettes DUMP-palettes.bin (a dump of the file select screen)')
        data = convert_indexed(args.picture, args.palettes)
    else:
        data = convert(args.scene, args.picture)
    if args.lz77:
        packed = lz77(data)
        if unlz77(packed) != data:
            sys.exit('LZ77 round trip failed')
        data = packed
    elif not args.raw:
        data = zlib.compress(data, 9)
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
