#!/usr/bin/env python3
"""Draws a room of maps.json the way the GBA's BG hardware would, for checking
the decoder against the game (and as the reference the viewer can be compared
with).

    python3 tools/layer-workbench/render_room.py AREA ROOM OUT.png [--layers BG2,BG1] [--entities]

AREA and ROOM accept hex (0x20) or decimal. Needs PIL only for writing the
PNG; compose() itself is plain Python. The PNG is the game's art: keep it out
of the repository.
"""
import argparse
import base64
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path=None):
    with open(path or os.path.join(HERE, "maps.json"), encoding="utf-8") as f:
        return json.load(f)


def find_room(maps, area, room):
    for a in maps["areas"]:
        if a["id"] == area:
            for r in a["rooms"]:
                if r["id"] == room:
                    return r
    return None


def u16s(b64):
    raw = base64.b64decode(b64)
    return struct.unpack("<%dH" % (len(raw) // 2), raw)


def bgr555(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def tilemap_of(maps, room, layer):
    """A layer as BG screen entries, (w8, h8, entries): metatile layers are
    expanded through their metatile set like RenderMapLayerToSubTileMap."""
    data = u16s(layer["data"])
    if layer["format"] == "tilemap":
        return layer["w"], layer["h"], list(data)
    sub = u16s(maps["metatiles"][room["metatiles"]][layer["metatileSet"]])
    w, h = layer["w"], layer["h"]
    out = [0] * (w * 2 * h * 2)
    for my in range(h):
        for mx in range(w):
            m = data[my * w + mx]
            base = (m & 0x3FFF) * 4
            q = sub[base:base + 4] if base + 4 <= len(sub) else (0, 0, 0, 0)
            for k, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
                out[(my * 2 + dy) * w * 2 + mx * 2 + dx] = q[k]
    return w * 2, h * 2, out


def compose(maps, room, layers=None):
    """RGB rows (list of bytearrays, width*3 each) of the room's visible layers
    in drawOrder (back to front), over the backdrop (BG palette 0 colour 0)."""
    gfx = maps["gfx"][room["gfx"]]
    vram = base64.b64decode(gfx["vram"])
    pal = [bgr555(c) for c in u16s(gfx["palettes"])]
    width, height = room["width"], room["height"]
    rows = [bytearray(pal[0] * width) for _ in range(height)]
    order = layers or room["drawOrder"]
    for bg in order:
        layer = room["layers"][bg]
        w8, h8, entries = tilemap_of(maps, room, layer)
        cb = (layer.get("charBase") or 0) * 0x4000
        c256 = layer.get("colors") == 256
        for ty in range(min(h8, (height + 7) // 8)):
            for tx in range(min(w8, (width + 7) // 8)):
                e = entries[ty * w8 + tx]
                t, hf, vf, pb = e & 0x3FF, e >> 10 & 1, e >> 11 & 1, e >> 12
                for y in range(8):
                    py = ty * 8 + y
                    if py >= height:
                        break
                    sy = 7 - y if vf else y
                    row = rows[py]
                    for x in range(8):
                        px = tx * 8 + x
                        if px >= width:
                            break
                        sx = 7 - x if hf else x
                        if c256:
                            o = cb + t * 64 + sy * 8 + sx
                            i = vram[o] if o < len(vram) else 0
                        else:
                            o = cb + t * 32 + sy * 4 + (sx >> 1)
                            i = (vram[o] >> (4 * (sx & 1))) & 15 if o < len(vram) else 0
                            i = pb * 16 + i if i else 0
                        if i:
                            row[px * 3:px * 3 + 3] = bytes(pal[i])
    return rows


def to_image(rows, width, height):
    from PIL import Image
    return Image.frombytes("RGB", (width, height), b"".join(bytes(r) for r in rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("area", type=lambda s: int(s, 0))
    ap.add_argument("room", type=lambda s: int(s, 0))
    ap.add_argument("out")
    ap.add_argument("--maps", default=None)
    ap.add_argument("--layers", default=None, help="comma list, back to front (default: drawOrder)")
    ap.add_argument("--entities", action="store_true", help="mark positioned entities")
    a = ap.parse_args()
    maps = load(a.maps)
    room = find_room(maps, a.area, a.room)
    if room is None:
        sys.exit("no room 0x%02X/%d in maps.json" % (a.area, a.room))
    img = to_image(compose(maps, room, a.layers.split(",") if a.layers else None),
                   room["width"], room["height"])
    if a.entities:
        from PIL import ImageDraw
        d = ImageDraw.Draw(img)
        for e in room["entities"]:
            if e["positioned"]:
                d.rectangle([e["x"] - 3, e["y"] - 3, e["x"] + 3, e["y"] + 3], outline=(255, 0, 255))
    img.save(a.out)
    print("%s: %dx%d, layers %s" % (a.out, room["width"], room["height"], a.layers or room["drawOrder"]))


if __name__ == "__main__":
    main()
