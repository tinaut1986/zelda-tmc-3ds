#!/usr/bin/env python3
"""Which rooms of the game border another room, and whether the neighbour shares its tileset (P4.9).

    survey.py ROM.gba [--region eu|usa] [--area N]

Reads the RoomHeader array of every area (map_x, map_y, pixel_width, pixel_height, tileSet_id; include/area.h,
offsets in port/port_rom.c) and lists, per area, the pairs of rooms whose rectangles share an edge, with
the length of that shared edge, and whether both rooms use the same tileSet_id (the neighbour's tiles can
then be drawn from the VRAM tiles already loaded). Prints a summary and the unusable pairs.
"""
import argparse
import re
import struct
import sys

OFFSETS = {'eu': 0x11D95C, 'usa': 0x11E214}
AREA_COUNT = 0x90


def area_names(path):
    names = {}
    try:
        text = open(path).read()
    except OSError:
        return names
    for m in re.finditer(r'\bAREA_([A-Z0-9_]+)\s*=?\s*(0x[0-9a-fA-F]+|\d+)?', text):
        pass
    return names


def rom_ptr(rom, off):
    value, = struct.unpack_from('<I', rom, off)
    return value & 0x1FFFFFF if 0x08000000 <= value < 0x0A000000 else None


def headers(rom, table, area):
    ptr = rom_ptr(rom, table + 4 * area)
    rooms = []
    while ptr is not None and len(rooms) < 64:
        first, = struct.unpack_from('<H', rom, ptr)
        if first == 0xFFFF:
            break
        x, y, w, h, tile = struct.unpack_from('<5H', rom, ptr)
        rooms.append((x, y, w, h, tile))
        ptr += 10
    return rooms


def shared_edge(a, b):
    """(side of a, length in pixels) where the rectangles touch, or None."""
    ax0, ay0, ax1, ay1 = a[0], a[1], a[0] + a[2], a[1] + a[3]
    bx0, by0, bx1, by1 = b[0], b[1], b[0] + b[2], b[1] + b[3]
    ox = min(ax1, bx1) - max(ax0, bx0)
    oy = min(ay1, by1) - max(ay0, by0)
    if ax1 == bx0 and oy > 0: return 'right', oy
    if bx1 == ax0 and oy > 0: return 'left', oy
    if ay1 == by0 and ox > 0: return 'below', ox
    if by1 == ay0 and ox > 0: return 'above', ox
    return None


def coverage(rooms, i):
    """For room i: the 8x8 cells of its 400x240 view window (the room centred in it, or the room's own size
    where it is larger) that lie outside the room, and how many a neighbour covers (same tileset / another)."""
    x, y, w, h, ts = rooms[i]
    vw, vh = max(w, 400), max(h, 240)
    wx0, wy0 = x - (vw - w) // 2, y - (vh - h) // 2
    outside = same = other = 0
    for cy in range(wy0, wy0 + vh, 8):
        for cx in range(wx0, wx0 + vw, 8):
            if x <= cx < x + w and y <= cy < y + h:
                continue
            outside += 1
            hit = None
            for j, r in enumerate(rooms):
                if j != i and r[0] <= cx < r[0] + r[2] and r[1] <= cy < r[1] + r[3]:
                    hit = r
                    break
            if hit is None:
                continue
            if hit[4] == ts:
                same += 1
            else:
                other += 1
    return outside, same, other


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom')
    ap.add_argument('--region', default='eu', choices=sorted(OFFSETS))
    ap.add_argument('--area', type=lambda s: int(s, 0))
    ap.add_argument('--coverage', action='store_true',
                    help='per area: how much of the black margin of a 400x240 view a neighbour could fill')
    args = ap.parse_args()
    rom = open(args.rom, 'rb').read()
    table = OFFSETS[args.region]
    total = same = 0
    if args.coverage:
        print('area  rooms with a margin   margin cells   same tileset   other tileset   nothing')
        gt = [0, 0, 0, 0]
        for area in range(AREA_COUNT) if args.area is None else [args.area]:
            rooms = headers(rom, table, area)
            n = out = sm = ot = 0
            for i, r in enumerate(rooms):
                if r[2] >= 400 and r[3] >= 240:
                    continue
                o, a, b = coverage(rooms, i)
                n += 1
                out += o
                sm += a
                ot += b
            if n:
                print('0x%02X  %3d                   %7d        %5.1f%%         %5.1f%%         %5.1f%%' % (
                    area, n, out, 100.0 * sm / out, 100.0 * ot / out, 100.0 * (out - sm - ot) / out))
                gt[0] += out; gt[1] += sm; gt[2] += ot
        print('all   margin cells %d: same tileset %.1f%%, other tileset %.1f%%, nothing %.1f%%' % (
            gt[0], 100.0 * gt[1] / gt[0], 100.0 * gt[2] / gt[0], 100.0 * (gt[0] - gt[1] - gt[2]) / gt[0]))
        return
    for area in range(AREA_COUNT) if args.area is None else [args.area]:
        rooms = headers(rom, table, area)
        pairs = []
        for i, a in enumerate(rooms):
            for j in range(i + 1, len(rooms)):
                edge = shared_edge(a, rooms[j])
                if edge:
                    pairs.append((i, j, edge, a[4] == rooms[j][4]))
        if not pairs:
            continue
        ok = sum(1 for p in pairs if p[3])
        total += len(pairs)
        same += ok
        print('area 0x%02X: %2d rooms, %2d bordering pairs, %2d share the tileset' % (area, len(rooms), len(pairs), ok))
        for i, j, (side, length), shared in pairs:
            a, b = rooms[i], rooms[j]
            print('    room %02d (%4d,%4d %3dx%3d ts %3d) %-5s room %02d (%4d,%4d %3dx%3d ts %3d) edge %3d px %s' % (
                i, a[0], a[1], a[2], a[3], a[4], side, j, b[0], b[1], b[2], b[3], b[4], length,
                '' if shared else 'DIFFERENT TILESET'))
    print('pairs: %d, same tileset: %d, different: %d' % (total, same, total - same))


if __name__ == '__main__':
    sys.exit(main())
