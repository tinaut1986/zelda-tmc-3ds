#!/usr/bin/env python3
"""ppm2png.py FILE.ppm... -> FILE.png at 2x (nearest), standard library only."""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, 'rb').read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    assert parts[0] == b'P6' and parts[3] == b'255'
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def write_png(path, w, h, rgb, scale=2):
    rows = []
    for y in range(h):
        line = rgb[y * w * 3:(y + 1) * w * 3]
        wide = b''.join(line[x * 3:x * 3 + 3] * scale for x in range(w))
        rows.extend([b'\x00' + wide] * scale)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(b''.join(rows), 9)))
        f.write(chunk(b'IEND', b''))


for p in sys.argv[1:]:
    w, h, rgb = read_ppm(p)
    out = p[:-4] + '.png'
    write_png(out, w, h, rgb)
    print(out)
