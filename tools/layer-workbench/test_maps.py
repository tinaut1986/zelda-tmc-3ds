#!/usr/bin/env python3
"""Checks the data build_maps.py extracts from the ROM.

    python3 tools/layer-workbench/test_maps.py [--rom PATH]   (or TMC_ROM=PATH)

A wrong decoder does not crash: it draws something plausible and false. So
these checks are about the data, not the code paths:

  - structure and counts (areas, rooms, skipped headers);
  - the names: enum counting agrees with the /*0xNN*/ comments in the headers,
    and the per-area BG setup still matches LoadRoomGfx's switch;
  - the label layout of data/map/entity_headers.s: every room's property
    table, read from the ROM's gAreaTable, lands exactly on a label, and every
    slot of it points where the .s says;
  - the entity decoder: every list decoded from the ROM equals the macros the
    .s writes for that label (kind, pool, id, type, params, x, y);
  - bounds: every metatile, 8x8 tile, palette and VRAM read stays inside what
    the room loaded, and every palette a visible layer uses was loaded;
  - entities inside their room, except a pinned list of data quirks;
  - a visual check against a real console screenshot of area 0x20 room 0
    (/tmp/tmc-shot/top.png, skipped when absent or without PIL), also for the
    curtain sprite of sprites.json;
  - sprites.json (sprites.py): the object definition table found in the ROM
    equals the one in src/objectDefinitions_data.inc, the sprite tables are
    within bounds, every drawn piece lies inside the gfx it reads and inside the
    palette rows it uses, each rule's source lines still exist, doors face the
    wall they stand on, and the pictures are pinned by checksum;
  - regression checksums of everything decoded. They pin what comes out
    today, after the checks above and a look at the rooms; they do not prove
    it right, they prove it did not change by accident. On a deliberate
    change the test prints the new values to paste.

maps.json is built first if missing (needs the ROM).
"""
import argparse
import base64
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tmcdata as T  # noqa: E402
import build_maps as B  # noqa: E402
import render_room as R  # noqa: E402
import sprites as SP  # noqa: E402
import entdefs as ED  # noqa: E402
import gzip  # noqa: E402

FAILS = []
CHECKS = [0]

SHOT = "/tmp/tmc-shot/top.png"


def check(cond, msg):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(msg)
    return cond


def equal(got, want, msg):
    return check(got == want, "%s: got %r, expected %r" % (msg, got, want))


def load(rom_path):
    path = os.path.join(HERE, "maps.json")
    if not os.path.isfile(path):
        if not rom_path:
            sys.exit("maps.json is missing; pass --rom or set TMC_ROM to build it")
        print("maps.json missing, building it...")
        subprocess.run([sys.executable, os.path.join(HERE, "build_maps.py"), "--rom", rom_path],
                       check=True, stdout=subprocess.DEVNULL)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_sprites(rom_path):
    path = os.path.join(HERE, "sprites.json")
    if not os.path.isfile(path):
        if not rom_path:
            return None
        print("sprites.json missing, building it...")
        subprocess.run([sys.executable, os.path.join(HERE, "build_maps.py"), "--rom", rom_path],
                       check=True, stdout=subprocess.DEVNULL)
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_tiles():
    path = os.path.join(HERE, "sprites_tiles.json.gz")
    if not os.path.isfile(path):
        return None
    with gzip.open(path, "rt", encoding="utf-8") as f:
        return json.load(f)


def rooms_of(maps):
    for a in maps["areas"]:
        for r in a["rooms"]:
            yield a, r


def b64u16(s):
    return R.u16s(s)


# ------------------------------------------------------------------ structure
EXPECTED = {
    "EU": {
        "areas": 91, "rooms": 597, "skipped": 20, "special": 35, "gfx": 128,
        "entities": 5135, "conditional": 813,
    },
}


def test_structure(maps):
    print("structure")
    equal(maps["format"], B.FORMAT, "format")
    equal(maps["version"], B.VERSION, "version")
    exp = EXPECTED.get(maps["rom"]["region"])
    check(exp is not None, "no expectations for region %s" % maps["rom"]["region"])
    if not exp:
        return
    equal(len(maps["areas"]), exp["areas"], "areas")
    equal(sum(len(a["rooms"]) for a in maps["areas"]), exp["rooms"], "rooms")
    equal(len(maps["skippedRooms"]), exp["skipped"], "skipped room headers")
    equal(sum(1 for _, r in rooms_of(maps) if r["mode"] == "special"), exp["special"], "special rooms")
    equal(len(maps["gfx"]), exp["gfx"], "distinct gfx sets")
    equal(sum(len(r["entities"]) for _, r in rooms_of(maps)), exp["entities"], "entities")
    equal(sum(1 for _, r in rooms_of(maps) for e in r["entities"] if e["when"] == "conditional"),
          exp["conditional"], "entities from callbacks")
    equal(maps["warnings"], [], "build warnings")
    ids = [(a["id"], r["id"]) for a, r in rooms_of(maps)]
    equal(len(ids), len(set(ids)), "room ids unique")
    for a in maps["areas"]:
        check(a["name"] is not None and a["name"].startswith("AREA_"), "area 0x%X has no enum name" % a["id"])
    # Minish house interiors are the 30 special rooms' main users, area 0x20.
    a20 = [r for a, r in rooms_of(maps) if a["id"] == 0x20]
    check(all(r["mode"] == "special" for r in a20), "area 0x20: all rooms special")


# ------------------------------------------------------------------ names
def test_names(maps):
    print("names: enums and the area switch")
    for rel, first, kind in (("include/enemy.h", "OCTOROK", "ENEMY"), ("include/npc.h", "NPC_NONE_0", "NPC")):
        comments = T.enum_hex_comments(rel, first)
        check(len(comments) > 50, "%s: hex comments found" % rel)
        ids = {v: int(k) for k, v in maps["enums"]["ids"][kind].items()}
        for name, value in comments.items():
            equal(ids.get(name), value, "%s %s counted vs /*0x..*/ comment" % (kind, name))
    equal(maps["enums"]["ids"]["ENEMY"].get(str(0x5E)), "CURTAIN", "ENEMY 0x5E")
    equal(maps["enums"]["ids"]["OBJECT"].get(str(0x58)), "GENTARI_CURTAIN", "OBJECT 0x58")
    equal(maps["enums"]["entityKinds"],
          {"1": "PLAYER", "3": "ENEMY", "4": "PROJECTILE", "6": "OBJECT", "7": "NPC",
           "8": "PLAYER_ITEM", "9": "MANAGER"}, "entity kinds")
    equal(maps["enums"]["areas"].get(str(0x20)), "AREA_MINISH_HOUSE_INTERIORS", "area 0x20")

    # LoadRoomGfx's switch (src/playerUtils.c) against build_maps' copy of it.
    text = T.strip_c_comments(T.read_text("src/playerUtils.c"))
    body = re.search(r"void LoadRoomGfx\(void\)\s*\{(.*?)\n\}", text, re.S).group(1)
    sw = body[body.index("switch (roomControls->area)"):]
    groups = [re.findall(r"case (AREA_\w+):", g) for g in sw.split("break;")]
    groups = [g for g in groups if g]
    equal(tuple(groups[0]), B.AREAS_8BPP_NO_BG1, "LoadRoomGfx: 8bpp areas")
    equal(tuple(groups[1]), B.AREAS_TOP_PRIO2, "LoadRoomGfx: top-priority-2 areas")
    check("|= 0x80" in sw.split("break;")[0] and "0xfdff" in sw.split("break;")[0],
          "LoadRoomGfx: first case still sets 256 colours and hides BG1")
    bg = T.read_text("src/backgroundAnimations.c")
    equal(re.search(r"gUnk_080B77C0\[\]\s*=\s*\{([^}]*)\}", bg).group(1).replace(" ", ""),
          "0x%x,0x%x,0x%x,0x1e03" % (B.BGCNT_BOTTOM, B.BGCNT_TOP, B.BGCNT_TOP_HOUSE), "gUnk_080B77C0")
    equal(B.common_palette_groups(), [11, 12], "LoadGfxGroups palette groups")

    named = sum(1 for _, r in rooms_of(maps) if r["enumName"])
    check(named >= 500, "rooms with a roomid.h name: %d" % named)


# ------------------------------------------------------------------ labels
def test_label_layout(rom, maps):
    print("labels: entity_headers.s laid out against the ROM")
    b = B.Builder(rom)
    table = rom.off["areaTable"]
    rooms = 0
    for a, r in rooms_of(maps):
        at = rom.table_entry(table, a["id"])
        p = rom.table_entry(at, r["id"]) if at is not None else None
        if p is None:
            check(r["properties"] is None, "0x%X/%d: properties without a table" % (a["id"], r["id"]))
            continue
        rooms += 1
        if not check(r["properties"] is not None, "0x%X/%d: property table 0x%X on no label"
                     % (a["id"], r["id"], p)):
            continue
        slots = b.structs[r["properties"]]
        check(len(slots) >= 8, "%s: %d slots" % (r["properties"], len(slots)))
        for i, sym in enumerate(slots):
            q = rom.u32(p + 4 * i)
            if isinstance(sym, int):
                equal(q, sym, "%s slot %d literal" % (r["properties"], i))
            elif sym in b.labels:
                equal(rom.ptr(q), b.labels[sym], "%s slot %d -> %s" % (r["properties"], i, sym))
            else:
                # A function (slots 4-7) or data outside entity_headers.s: a ROM pointer.
                check(q == 0 or rom.ptr(q) is not None, "%s slot %d (%s) not a ROM pointer"
                      % (r["properties"], i, sym))
    check(rooms > 500, "rooms with property tables: %d" % rooms)


# ------------------------------------------------------------------ entities
def _num(v):
    v = v.strip()
    if not re.match(r"^[\s0-9a-fA-FxX|<>()+\-*&~]+$", v):
        return None
    try:
        return int(eval(v, {"__builtins__": {}}))   # numbers and operators only
    except Exception:
        return None


def _args(text):
    pos, kw = [], {}
    for a in [x.strip() for x in text.split(",") if x.strip()]:
        if "=" in a:
            k, v = a.split("=", 1)
            kw[k.strip()] = v.strip()
        else:
            pos.append(a)
    return pos, kw


RAW_MACROS = {"entity_raw": (None, None), "object_raw": (6, 0xF), "enemy_raw": (3, 0xF),
              "npc_raw": (7, 0x4F), "manager": (9, 0xF), "projectile_raw": (4, 0xF)}


def expected_entity(word, args):
    """The 16 bytes an entity macro emits, as {field: value or None}."""
    pos, kw = _args(args)
    g = lambda k, d=0: _num(kw[k]) if k in kw else d
    if word in RAW_MACROS:
        kind, pool = RAW_MACROS[word]
        kind = g("type") if kind is None else kind
        pool = g("pool", pool) if word != "entity_raw" else g("pool")
        c = g("collision")
        pc = kw.get("script" if word == "npc_raw" else "paramC", "0")
        return {"kindByte": None if kind is None or c is None else kind | (c << 4), "pool": pool,
                "id": g("subtype"), "type": g("paramA"), "paramB": g("paramB"),
                "x": g("x"), "y": g("y"), "paramC": _num(pc)}
    names = {"object_item": ("x", "y", "item", "flag", "collision"),
             "enemy_chuchu": ("x", "y", "bound_x", "bound_y", "bound_tile_width", "bound_tile_height", "collision"),
             "enemy_octorok": ("x", "y", "bound_x", "bound_y", "bound_tile_width", "bound_tile_height", "collision"),
             "enemy_tree_item": ("x", "y", "unknown", "collision"),
             "object_minish_lilypad": ("x", "y", "room_property", "collision")}.get(word)
    if names:
        v = dict(zip(names, pos))
        v.update(kw)
        n = {k: _num(x) for k, x in v.items()}
        c = n.get("collision") or 0
    if word == "object_item":
        flag = n.get("flag")
        return {"kindByte": 6 | (c << 4), "pool": 0xF, "id": 0, "type": n.get("item"),
                "paramB": 0x0400, "x": n.get("x"), "y": n.get("y"),
                "paramC": None if flag is None else flag << 16}
    if word in ("enemy_chuchu", "enemy_octorok"):
        bw, bh, bx, by = (n.get(k) for k in ("bound_tile_width", "bound_tile_height", "bound_x", "bound_y"))
        return {"kindByte": 3 | (c << 4), "pool": 0xF,
                "id": 1 if word == "enemy_chuchu" else 0, "type": 0,
                "paramB": None if None in (bw, bh) else (bw << 16) | (bh << 24),
                "x": n.get("x"), "y": n.get("y"),
                "paramC": None if None in (bx, by) else bx | (by << 16)}
    if word == "enemy_tree_item":
        return {"kindByte": 3 | (c << 4), "pool": 0xF, "id": 0x65, "type": n.get("unknown"),
                "paramB": 0, "x": n.get("x"), "y": n.get("y"), "paramC": 0}
    if word == "object_minish_lilypad":
        return {"kindByte": 6 | (c << 4), "pool": 0xF, "id": 0x73, "type": 0,
                "paramB": n.get("room_property"), "x": n.get("x"), "y": n.get("y"), "paramC": 0}
    if word == "ezlo_hint":
        return {"kindByte": 9, "pool": 0, "id": 10}
    return None


def test_entities_vs_source(rom, maps):
    print("entities: ROM lists against the macros of entity_headers.s")
    records = []
    T.layout_entity_headers(rom.region, records)
    by_label = {}
    for label, off, word, args in records:
        by_label.setdefault(label, []).append((word, args))
    compared = mismatched = 0
    seen = set()
    for a, r in rooms_of(maps):
        lists = {}
        for e in r["entities"]:
            if e["label"]:
                lists.setdefault((e["label"], e["list"], e.get("loadedBy")), []).append(e)
        for (label, _, _), ents in lists.items():
            if label in seen:
                continue
            seen.add(label)
            src = [w for w in by_label.get(label, []) if w[0] != "entity_list_end"]
            src = by_label.get(label, [])
            exp = []
            for word, args in src:
                if word == "entity_list_end":
                    break
                exp.append((word, args))
            if not equal(len(ents), len(exp), "%s: entity count" % label):
                mismatched += 1
                continue
            for e, (word, args) in zip(ents, exp):
                want = expected_entity(word, args)
                if not check(want is not None, "%s: unknown macro %s" % (label, word)):
                    continue
                got = {"kindByte": int(e["raw"][0:2], 16), "pool": int(e["raw"][2:4], 16), "id": e["id"],
                       "type": e["type"], "paramB": e["paramB"], "x": e["x"], "y": e["y"],
                       "paramC": e["paramC"]}
                for k, v in want.items():
                    if v is None:
                        continue
                    compared += 1
                    if not equal(got[k], v, "%s[%d] %s %s" % (label, e["index"], word, k)):
                        mismatched += 1
    check(compared > 30000, "entity fields compared: %d" % compared)
    print("  %d labelled lists, %d fields compared, %d mismatches" % (len(seen), compared, mismatched))


# ------------------------------------------------------------------ bounds
def test_bounds(maps):
    print("bounds: metatiles, tiles, palettes, VRAM")
    unused_palette = 0
    overflow = {}
    for a, r in rooms_of(maps):
        where = "0x%X/%d" % (a["id"], r["id"])
        gfx = maps["gfx"][r["gfx"]]
        vram_len = len(base64.b64decode(gfx["vram"]))
        equal(vram_len, T.BG_VRAM_SIZE, where + " VRAM size")
        equal(len(base64.b64decode(gfx["palettes"])), 1024, where + " palette size")
        mt = maps["metatiles"][r["metatiles"]]
        check(r["width"] % 16 == 0 and r["height"] % 16 == 0, where + " size multiple of 16")
        check(0 < r["width"] <= 1024 and 0 < r["height"] <= 1024, where + " size within 64x64 metatiles")
        check(set(r["drawOrder"]) <= set(r["layers"]), where + " drawOrder names layers")
        for bg, layer in r["layers"].items():
            data = b64u16(layer["data"])
            equal(len(data), layer["w"] * layer["h"], "%s %s data length" % (where, bg))
            if layer["format"] == "metatile" and not layer["defined"]:
                check(not layer["visible"], "%s %s: undefined layer shown" % (where, bg))
                continue
            if layer["format"] == "metatile":
                equal((layer["w"], layer["h"]), (r["width"] // 16, r["height"] // 16), where + " metatile dims")
                sub = b64u16(mt[layer["metatileSet"]])
                n = len(sub) // 4
                bad = [v for v in data if v >= n]
                if bad:
                    overflow["%02X/%d/%s" % (a["id"], r["id"], bg)] = len(bad)
                types = b64u16(mt[layer["metatileSet"] + "Types"])
                check(len(types) >= n, "%s %s: tile types cover the metatiles" % (where, bg))
            elif bg != "BG3":
                equal((layer["w"], layer["h"]), ((r["width"] + 7) // 8, (r["height"] + 7) // 8),
                      where + " tilemap dims")
            if not layer["visible"]:
                continue
            w8, h8, entries = R.tilemap_of(maps, r, layer)
            cb = layer["charBase"] * 0x4000
            step = 64 if layer["colors"] == 256 else 32
            used_pals = set()
            for e in entries:
                t = e & 0x3FF
                check(cb + (t + 1) * step <= vram_len, "%s %s: tile 0x%X past VRAM" % (where, bg, t))
                if layer["colors"] == 16:
                    used_pals.add(e >> 12)
            for p in used_pals:
                if not gfx["paletteLoaded"] >> p & 1:
                    unused_palette += 1
                    check(False, "%s %s: uses BG palette %d that was never loaded" % (where, bg, p))
            if layer["colors"] == 256:
                check(gfx["paletteLoaded"] & 0xFFFF == 0xFFFF, where + ": 256-colour layer with all BG palettes")
        for e in r["tileEntities"]:
            check(e["typeName"] is not None, "%s: tile entity type %d unnamed" % (where, e["type"]))
    # Metatile indices past the end of the area's loaded metatile set. The game
    # reads whatever follows in gMapTop/gMapBottom.subTiles (stale data, or for
    # 0x81D past TILESET_SIZE the next field). Three rooms do it, two of them in
    # unused areas (0x77, 0x87); pinned so a decoder change that adds any fails.
    equal(overflow, KNOWN_METATILE_OVERFLOW.get(maps["rom"]["region"], {}),
          "metatile indices past the loaded set (pinned)")


def outside_key(a, r, e):
    return "%02X/%d/%s" % (a["id"], r["id"], e["when"])


KNOWN_METATILE_OVERFLOW = {"EU": {"13/3/BG1": 1, "77/0/BG1": 260, "87/0/BG2": 5}}

KNOWN_OUTSIDE_EU = {
    # area/room/list kind: count. Looked at one by one:
    "09/0/conditional": 2,          # Royal Valley: KEY_STEALING_TAKKURI crows fly in from below
    "0B/1/afterTabidachi": 9,       # Lake Hylia beanstalk room points at the main room's octoroks
    "0D/16/always": 1, "0D/17/always": 1, "0D/18/always": 1, "0D/19/always": 1,
    "0D/20/always": 1,              # beanstalk tops: BEANSTALK object 24 px below the bottom edge
    "19/1/tile/SMALL_CHEST": 1,     # a chest at tilePos 0xA73 (y 656) in a 432-high room
    "45/0/always": 2, "67/0/always": 2,   # unused areas 0x45/0x67: LOCKED_DOORs at (344..392, 520)
    "58/35/afterTabidachi": 4, "60/1/afterTabidachi": 4,
    "68/1/always": 4,               # TORCH_TRAP: its y field is a parameter (0xF0xx), not a position
    "60/49/always": 1,              # Temple of Droplets: a kind-5 record at x 0x600
    # Festival Town (400x960) shares Hyrule Town's (1008x960) callback and chests.
    "15/0/always": 1, "15/0/conditional": 22, "15/0/tile/SMALL_CHEST": 2,
}


def test_entities(maps):
    print("entities: kinds, names, positions")
    outside = {}
    curtain = None
    for a, r in rooms_of(maps):
        for e in r["entities"]:
            if e["kind"] != 5:      # the one kind-5 record (Temple of Droplets) is in the data as such
                check(e["kindName"] is not None, "0x%X/%d: unknown kind %d" % (a["id"], r["id"], e["kind"]))
            if e["kindName"] in maps["enums"]["ids"]:
                check(e["idName"] is not None, "0x%X/%d: %s id %d unnamed"
                      % (a["id"], r["id"], e["kindName"], e["id"]))
            if e["positioned"] and not (0 <= e["x"] <= r["width"] and 0 <= e["y"] <= r["height"]):
                k = outside_key(a, r, e)
                outside[k] = outside.get(k, 0) + 1
            if a["id"] == 0x20 and r["id"] == 0 and e["idName"] == "GENTARI_CURTAIN":
                curtain = e
        for t in r["tileEntities"]:
            if t["x"] is not None:
                if not (0 <= t["x"] <= r["width"] and 0 <= t["y"] <= r["height"]):
                    k = "%02X/%d/tile/%s" % (a["id"], r["id"], t["typeName"])
                    outside[k] = outside.get(k, 0) + 1
    known = KNOWN_OUTSIDE_EU if maps["rom"]["region"] == "EU" else {}
    equal(outside, known, "entities outside their room (pinned quirks)")
    print("  outside their room (pinned): %d" % sum(outside.values()))
    # The curtain on the left wall of Gentari's room (area 0x20 room 0).
    if check(curtain is not None, "0x20/0: OBJECT GENTARI_CURTAIN present"):
        equal((curtain["kindName"], curtain["id"], curtain["x"], curtain["y"], curtain["when"]),
              ("OBJECT", 0x58, 0x18, 0x50, "always"), "0x20/0 curtain")
        check(curtain["positioned"], "curtain is positioned")



def test_exits(maps):
    print("exits: area warps lead to rooms that exist")
    rooms = {(a["id"], r["id"]) for a, r in rooms_of(maps)}
    n = 0
    for a, r in rooms_of(maps):
        for x in r["exits"]:
            check(x["warpTypeName"] is not None, "0x%X/%d: warp type %d unnamed" % (a["id"], r["id"], x["warpType"]))
            if x["warpTypeName"] in ("WARP_TYPE_AREA", "WARP_TYPE_AREA2"):
                n += 1
                check((x["destArea"], x["destRoom"]) in rooms, "0x%X/%d: exit to missing room 0x%X/%d"
                      % (a["id"], r["id"], x["destArea"], x["destRoom"]))
                check(0 <= x["x"] <= r["width"] and 0 <= x["y"] <= r["height"],
                      "0x%X/%d: exit trigger outside the room" % (a["id"], r["id"]))
    check(n > 350, "area warps: %d" % n)


# ------------------------------------------------------------------ visual
def screenshot_setup(maps):
    """(shot pixels, placed rooms, per-channel colour mapping) or None when the
    screenshot or PIL is not there."""
    if not os.path.isfile(SHOT):
        print("  skipped: %s not found" % SHOT)
        return None
    try:
        from PIL import Image
    except ImportError:
        print("  skipped: no PIL")
        return None
    shot = Image.open(SHOT).convert("RGB")
    sp = shot.load()
    # The 3DS shot is 400x240 in wide mode: room 0 (240x160) at x 160, y 40,
    # its left neighbour room 1 at x -80. The console applies a colour
    # correction, so colours are compared through a per-channel mapping
    # learned from the two rooms; what must match is which pixel gets which
    # colour, not the exact RGB.
    placed = []
    for room_id, ox in ((0, 160), (1, -80)):
        r = R.find_room(maps, 0x20, room_id)
        rows = R.compose(maps, r)
        placed.append((room_id, ox, 40, r, rows))
    chan = [dict() for _ in range(3)]
    for _, ox, oy, r, rows in placed:
        for y in range(r["height"]):
            for x in range(r["width"]):
                if 0 <= x + ox < 400:
                    q = rows[y][x * 3:x * 3 + 3]
                    p = sp[x + ox, y + oy]
                    for c in range(3):
                        d = chan[c].setdefault(q[c], {})
                        d[p[c]] = d.get(p[c], 0) + 1
    cmap = [{k: max(v, key=v.get) for k, v in ch.items()} for ch in chan]
    return sp, placed, cmap


def test_visual(maps):
    print("visual: area 0x20 rooms 0 and 1 against the console screenshot")
    setup = screenshot_setup(maps)
    if setup is None:
        return
    sp, placed, cmap = setup
    for room_id, ox, oy, r, rows in placed:
        ok = tot = 0
        for y in range(r["height"]):
            for x in range(r["width"]):
                if 0 <= x + ox < 400:
                    tot += 1
                    q = rows[y][x * 3:x * 3 + 3]
                    p = sp[x + ox, y + oy]
                    ok += all(abs(cmap[c][q[c]] - p[c]) <= 2 for c in range(3))
        share = ok / tot
        # Room 0's differences are its sprites (Gentari, Link, the curtain, two
        # archway glows) and the HUD; room 1's the archway glows.
        need = 0.90 if room_id == 0 else 0.97
        check(share >= need, "0x20/%d matches the screenshot on %.1f%% of pixels (need %.0f%%)"
              % (room_id, share * 100, need * 100))
        print("  room %d: %.1f%% of %d pixels match" % (room_id, share * 100, tot))
    # The channel mapping must be monotonic: a brighter source never maps darker.
    for c in range(3):
        ks = sorted(cmap[c])
        check(all(cmap[c][a] <= cmap[c][b] for a, b in zip(ks, ks[1:])), "colour mapping channel %d monotonic" % c)



# ------------------------------------------------------------------ sprites
# Drawn sprites per family in the EU ROM, pinned (a change means a rule or a table moved).
SPRITE_COUNTS = {
    "EU": {"OBJECT/BOSS_DOOR": 18, "OBJECT/CHEST_SPAWNER": 29, "OBJECT/GENTARI_CURTAIN": 1,
           "OBJECT/LIGHT_DOOR": 4, "OBJECT/LOCKED_DOOR": 141, "OBJECT/METAL_DOOR": 1,
           "OBJECT/MINECART_DOOR": 14, "OBJECT/TRAPDOOR": 1},
}
# Default sprites (kinds with a definition table) per kind, and how many entities of those kinds got none, pinned.
DEFAULT_COUNTS = {
    "EU": {"drawn": {"ENEMY": 1047, "NPC": 406, "OBJECT": 1197, "PROJECTILE": 65}, "skipped": 1337},
}
OBJ_TILES_FREE = 0x400 - 0x180      # OBJ VRAM tiles from the first gfx slot (slot 4: 0x140 + 4 * 0x10)
MAX_PIECES = 0x80                   # gOAMControls.oam entries


def _mirror_v(pic):
    w, h = pic["w"], pic["h"]
    raw = base64.b64decode(pic["rgba"])
    return b"".join(raw[(h - 1 - y) * w * 4:(h - y) * w * 4] for y in range(h))


def test_sprites(rom, maps, spr, tiles=None):
    print("sprites")
    equal(spr["format"], "tmc-layer-workbench-sprites", "sprites.json format")
    equal(spr["rom"]["sha1"], maps["rom"]["sha1"], "sprites.json was built from the same ROM as maps.json")
    rooms = {"%02X-%02X" % (a["id"], r["id"]): r for a, r in rooms_of(maps)}
    area_of = {"%02X-%02X" % (a["id"], r["id"]): a["id"] for a, r in rooms_of(maps)}

    # --- structure: every record names a positioned entity of its family; pictures are sane
    drawn = {}
    n_rec = 0
    for key, recs in spr["rooms"].items():
        room = rooms.get(key)
        if not check(room is not None, "sprites: room %s does not exist" % key):
            continue
        for idx, rec in recs.items():
            n_rec += 1
            e = room["entities"][int(idx)] if int(idx) < len(room["entities"]) else None
            if not check(e is not None, "sprites %s#%s: no such entity" % (key, idx)):
                continue
            equal("%s/%s" % (e["kindName"], e["idName"]), rec["family"], "sprites %s#%s family" % (key, idx))
            check(e["positioned"], "sprites %s#%s: entity is not positioned" % (key, idx))
            check(("pic" in rec) != ("skipped" in rec), "sprites %s#%s: needs exactly one of pic / skipped" % (key, idx))
            if "skipped" in rec:
                check(bool(rec["skipped"]), "sprites %s#%s: empty skip reason" % (key, idx))
                continue
            pic = spr["pictures"].get(rec["pic"])
            if not check(pic is not None, "sprites %s#%s: picture %s missing" % (key, idx, rec["pic"])):
                continue
            drawn.setdefault(rec["family"], []).append((key, idx, rec, pic, e, room))
            check(rec.get("default", False) == (rec["family"].split("/")[0] in SP.DEFAULT_KINDS and
                                                tuple(rec["family"].split("/", 1)) not in SP.RULES),
                  "sprites %s#%s: default flag does not match the family having a curated rule" % (key, idx))
            check(("src" in rec) and rec["src"] in spr["sources"], "sprites %s#%s: no source for the picker" % (key, idx))
            raw = base64.b64decode(pic["rgba"])
            check(len(raw) == pic["w"] * pic["h"] * 4, "sprites %s#%s: picture data length" % (key, idx))
            check(1 <= pic["w"] <= 256 and 1 <= pic["h"] <= 256 and abs(pic["ox"]) <= 256 and abs(pic["oy"]) <= 256,
                  "sprites %s#%s: picture size/offset %dx%d at %d,%d" % (key, idx, pic["w"], pic["h"], pic["ox"], pic["oy"]))
            alphas = set(raw[3::4])
            check(alphas <= {0, 255} and 255 in alphas, "sprites %s#%s: picture is empty or has partial alpha" % (key, idx))
            x0, y0 = e["x"] + pic["ox"], e["y"] + pic["oy"]
            check(x0 < room["width"] and y0 < room["height"] and x0 + pic["w"] > 0 and y0 + pic["h"] > 0,
                  "sprites %s#%s: picture does not touch the room" % (key, idx))
            if rec.get("default"):
                check(rec["rule"].startswith("default (not curated): "), "sprites %s#%s: a default record must say so" % (key, idx))
            for k in ("rule", "why", "state", "sprite", "frame", "flipX", "flipY", "gfx", "palette", "baseFlipX", "baseFlipY", "dx", "dy"):
                check(k in rec, "sprites %s#%s: record lacks %s" % (key, idx, k))
            check(rec["why"] and all(isinstance(w, str) for w in rec["why"]), "sprites %s#%s: empty why" % (key, idx))
    want = SPRITE_COUNTS.get(maps["rom"]["region"])
    got = {f: len(v) for f, v in drawn.items() if not v[0][2].get("default")}
    if want is not None:
        equal(got, want, "drawn curated sprites per family")
    for fam in ("%s/%s" % k for k in SP.RULES):
        check(fam in drawn, "sprites: no %s drawn at all" % fam)
    by_kind = {}
    for f, v in drawn.items():
        if v[0][2].get("default"):
            by_kind[f.split("/")[0]] = by_kind.get(f.split("/")[0], 0) + len(v)
    skipped_default = sum(1 for recs in spr["rooms"].values() for r in recs.values() if r.get("default") and "skipped" in r)
    wd = DEFAULT_COUNTS.get(maps["rom"]["region"])
    if wd is not None:
        equal(by_kind, wd["drawn"], "default sprites drawn per kind")
        equal(skipped_default, wd["skipped"], "default sprites left as markers")
    # every named OBJECT / ENEMY / NPC / PROJECTILE entity that is positioned has a record, except the families
    # NOT_COVERED names (they keep their marker and say why)
    missing = 0
    for key, room in rooms.items():
        recs = spr["rooms"].get(key, {})
        for i, e in enumerate(room["entities"]):
            if e["positioned"] and e["kindName"] in SP.DEFAULT_KINDS and (e["kindName"], e["idName"]) not in SP.NOT_COVERED \
                    and str(i) not in recs:
                missing += 1
    equal(missing, 0, "entities of a kind with a definition table that have neither a sprite nor a stated reason")
    for fam in SP.NOT_COVERED:
        check(not any(r.get("default") and r["family"] == "%s/%s" % fam for recs in spr["rooms"].values() for r in recs.values()),
              "sprites: %s is NOT_COVERED and must stay a marker" % (fam,))
    print("  %d records, %d drawn (%d default: %s), %d default entities left as markers, %d pictures"
          % (n_rec, sum(got.values()) + sum(by_kind.values()), sum(by_kind.values()),
             ", ".join("%s %d" % kv for kv in sorted(by_kind.items())), skipped_default, len(spr["pictures"])))
    test_catalog(spr, tiles, drawn, SP.SpriteTables(rom) if rom is not None else None)

    # --- every rule's lines still exist in the decomp
    for fam, checks in SP.RULE_SOURCES.items():
        check(fam in SP.RULES, "RULE_SOURCES names a family without a rule: %s" % (fam,))
        for rel, rx in checks:
            check(re.search(rx, T.strip_c_comments(T.read_text(rel))) is not None,
                  "%s/%s: %s no longer matches /%s/ (the rule may be stale)" % (fam[0], fam[1], rel, rx))
    for fam in SP.RULES:
        check(fam in SP.RULE_SOURCES, "rule without source checks: %s" % (fam,))

    # --- doors face the wall they stand on: a door within 48 px of an edge of its room has the direction of that edge
    # (type & 3: 0 north, 1 east, 2 south, 3 west, as direction = type << 3 in the init code); the drawn flip follows.
    near = 0
    for fam in ("OBJECT/BOSS_DOOR", "OBJECT/LOCKED_DOOR", "OBJECT/MINECART_DOOR"):
        for key, idx, rec, pic, e, room in drawn.get(fam, []):
            w, h, x, y = room["width"], room["height"], e["x"], e["y"]
            dist = {0: y, 1: w - x, 2: h - y, 3: x}
            wall = min(dist, key=dist.get)
            if dist[wall] <= 48:
                near += 1
                check(wall == e["type"] & 3, "%s %s#%s: type 0x%02X faces %s but stands at the %s edge" %
                      (fam, key, idx, e["type"], SP.DIRS[e["type"] & 3], SP.DIRS[wall]))
    check(near > 100, "doors near a wall checked: %d" % near)
    for key, idx, rec, pic, e, room in drawn.get("OBJECT/METAL_DOOR", []):
        check(rec["flipY"] == 1 and e["y"] > room["height"] / 2, "metal door %s#%s: flipY on the south wall" % (key, idx))
    # a south boss door is the vertical mirror of a north one of the same dungeon (flipY from the animation's flags)
    by_dir = {}
    for key, idx, rec, pic, e, room in drawn.get("OBJECT/BOSS_DOOR", []):
        if e["type"] in (0, 2):
            by_dir.setdefault((area_of[key], e["type"]), pic)
    pairs = 0
    for (area, t), pic in sorted(by_dir.items()):
        other = by_dir.get((area, 2))
        if t == 0 and other:
            pairs += 1
            check(pic["w"] == other["w"] and pic["h"] == other["h"] and _mirror_v(pic) == base64.b64decode(other["rgba"]),
                  "area 0x%02X: the south boss door is not the vertical mirror of the north one" % area)
    print("  %d dungeons with both a north and a south boss door: mirror images" % pairs)

    if rom is None:
        print("  ROM tables: skipped (no --rom / TMC_ROM)")
        return drawn
    # --- the tables, from the ROM
    t = SP.SpriteTables(rom)
    inc = t.defs.inc
    equal(len(inc), len(maps["enums"]["ids"]["OBJECT"]), "objectDefinitions_data.inc has one entry per OBJECT id")
    mism = 0
    for i, n in enumerate(inc):
        if n is not None and SP.encode_definition(n) != bytes(rom.data[t.defs.base + 8 * i:t.defs.base + 8 * i + 8]):
            mism += 1
    equal(mism, 0, "gObjectDefinitions in the ROM vs the source, entries that differ")
    check(sum(1 for n in inc if n is not None) > 100, "object definitions compared")
    # NPCs: same record and form rules as objects, from src/npcDefinitions_data.inc
    npc = SP.NPCDefinitions(rom)
    equal(len(npc.inc), len(maps["enums"]["ids"]["NPC"]), "npcDefinitions_data.inc has one entry per NPC id")
    mism = sum(1 for i, n in enumerate(npc.inc)
               if n is not None and SP.encode_definition(n) != bytes(rom.data[npc.base + 8 * i:npc.base + 8 * i + 8]))
    equal(mism, 0, "gNPCDefinitions in the ROM vs the source, entries that differ")
    for tag, tbl in (("object", t.defs), ("NPC", npc)):
        bad = n = 0
        for i, name in enumerate(tbl.multi):
            if not name:
                continue
            at = rom.read_ptr(tbl.base + 8 * i + 4)
            check(at is not None, "%s definition %d: form table pointer" % (tag, i))
            for k, f in enumerate(tbl.forms[name]):
                if f is not None:
                    n += 1
                    bad += SP.encode_definition(f) != bytes(rom.data[at + 8 * k:at + 8 * k + 8])
        check(n > 100, "%s forms compared: %d" % (tag, n))
        equal(bad, 0, "%s forms in the ROM vs the source, entries that differ" % tag)
    # enemies and projectiles: 16-byte records located by content, then compared entry by entry
    for kind, floor in (("ENEMY", 200), ("PROJECTILE", 70)):
        cd = ED.CDefinitions(rom, kind)
        check(len(maps["enums"]["ids"][kind]) - 1 <= len(cd.items) <= len(maps["enums"]["ids"][kind]),
              "%s: the C table has one entry per id (%d for %d ids)" % (kind, len(cd.items), len(maps["enums"]["ids"][kind])))
        compared, lits, problems = cd.compare_with_source()
        equal(problems, [], "%s definitions in the ROM vs the source" % kind)
        check(compared >= floor, "%s records compared: %d" % (kind, compared))
        print("  %s definitions at 0x%X: %d records compared with the source (%d sprite literals not comparable)"
              % (kind, cd.base, compared, lits))
    spp = SP.port_config_defines()
    equal(t.sprite_ptr_count, spp["PORT_%s_SPRITE_PTR_COUNT" % rom.region], "gSpritePtrs entries vs port_config.h")
    text = T.strip_c_comments(T.read_text("port/port_rom_tables.c"))
    m = re.search(r"kOverlaySizeData\[240\]\s*=\s*\{(.*?)\};", text, re.S)
    equal(bytes(int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", m.group(1))), t.size_table,
          "OBJ size table in the ROM vs port kOverlaySizeData")
    door_vals, at = SP.locked_door_gfx_table(rom)
    equal(len(door_vals), 80, "gUnk_0811F690 length")
    # the frame-object list ends where the port says
    last = max(struct.unpack_from("<I", rom.data, t.frame_lists + 4 * i)[0] for i in range(t.frame_lists_count))
    check(last < t.frame_lists_size, "gFrameObjLists: frame tables inside the list")

    # --- every drawn piece: frame in its table, tiles inside the gfx and OBJ VRAM, palette row available, and the
    # picture is what the renderer makes of it
    gfx_sets = {k: {"palettes": B.u16s(base64.b64decode(g["palettes"])), "mask": g["paletteLoaded"]}
                for k, g in maps["gfx"].items()}
    pieces_total = 0
    for fam, lst in drawn.items():
        for key, idx, rec, pic, e, room in lst:
            tag = "%s %s#%s" % (fam, key, idx)
            sprite, frame = rec["sprite"], rec["frame"]
            extent = t.frame_table_extent(sprite)
            check(extent is None or frame < extent, "%s: frame %d past the %s frames of sprite %d" % (tag, frame, extent, sprite))
            pcs = t.pieces(sprite, frame)
            if not check(pcs, "%s: no pieces" % tag):
                continue
            pieces_total += len(pcs)
            check(len(pcs) <= MAX_PIECES, "%s: %d pieces" % (tag, len(pcs)))
            mm = re.match(r"fixed (\d+)", rec["gfx"])
            if mm:
                blob = t.fixed_blob(int(mm.group(1)))
            else:
                blob = t.swap_tiles(sprite, frame)
            if not check(blob is not None, "%s: no gfx (%s)" % (tag, rec["gfx"])):
                continue
            check(len(blob) // 32 <= OBJ_TILES_FREE, "%s: gfx of %d tiles does not fit OBJ VRAM" % (tag, len(blob) // 32))
            gs = gfx_sets[room["gfx"]]
            rows = t.obj_palette(rec["palette"], gs["palettes"], gs["mask"])
            if not check(rows is not None, "%s: OBJ palette %d unknown" % (tag, rec["palette"])):
                continue
            for p in pcs:
                tw, th = (t.size_table[4 * (p["shape"] * 4 + p["size"]) + 2] // 8,
                          t.size_table[4 * (p["shape"] * 4 + p["size"]) + 3] // 8)
                check(p["shape"] != 3, "%s: OBJ shape 3" % tag)
                check(p["tile"] + tw * th <= len(blob) // 32, "%s: piece tiles %d+%d past the %d of the gfx" %
                      (tag, p["tile"], tw * th, len(blob) // 32))
                if p["clearPal"]:
                    check(t.abs_palette_row(p["palAdd"], gs["palettes"], gs["mask"]) is not None,
                          "%s: absolute palette row %d not loaded" % (tag, p["palAdd"]))
                else:
                    check(p["palAdd"] < len(rows), "%s: palette row +%d of %d" % (tag, p["palAdd"], len(rows)))
            again, problems = SP.render_pieces(t, pcs, blob, {"rows": rows, "pal": gs["palettes"], "loaded": gs["mask"]},
                                               rec["flipX"], rec["flipY"], rec["dx"], rec["dy"])
            check(again is not None and (again[0], again[1], again[2], again[3]) == (pic["w"], pic["h"], pic["ox"], pic["oy"])
                  and base64.b64encode(again[4]).decode("ascii") == pic["rgba"], "%s: picture differs from a fresh render" % tag)
    print("  %d OAM pieces checked against their gfx and palette" % pieces_total)
    return drawn


class _CatalogTables:
    """What render_pieces needs of SpriteTables, from the JSON alone (the way the viewer has it)."""

    def __init__(self, spr, src):
        self.size_table = bytes(spr["sizeTable"])
        self._abs = src.get("a")

    def abs_palette_row(self, row, pal, loaded):
        return self._abs[row] if self._abs and row < len(self._abs) else None


def rebuild_picture(spr, tiles, rec):
    """The record's picture re-rendered from sprites.json's catalog (frames, animations, sources, tilesets) and the
    swapped pools of sprites_tiles.json.gz: what the viewer's own renderer works from. Returns (w, h, ox, oy, rgba)
    or None."""
    src = spr["sources"][rec["src"]]
    cat = spr["sprites"][str(src["s"])]
    pieces = [{"x": p[0], "y": p[1], "shape": p[2], "size": p[3], "hflip": p[4] & 1, "vflip": (p[4] >> 1) & 1,
               "clearPal": (p[4] >> 2) & 1, "tile": p[5], "palAdd": p[6]} for p in cat["frames"][str(rec["frame"])]]
    if src["m"] == 0:
        blob = base64.b64decode(spr["tilesets"][src["t"]])
    else:
        n, first = cat["swap"][str(rec["frame"])]
        blob = base64.b64decode(tiles["pools"][str(src["s"])])[32 * first:32 * (first + n)]
    pic, _ = SP.render_pieces(_CatalogTables(spr, src), pieces, blob, {"rows": src["r"], "pal": None, "loaded": 0},
                              rec["flipX"], rec["flipY"], rec["dx"], rec["dy"])
    return pic


def test_catalog(spr, tiles, drawn, tables=None):
    """The data the sprite picker draws alternatives from: bounds, and that it reproduces every stored picture."""
    print("sprite catalog")
    cats, srcs = spr["sprites"], spr["sources"]
    equal(len(spr["sizeTable"]), 240, "size table length")
    n_frames = n_pieces = n_anims = unlisted = 0
    for k, c in cats.items():
        for f, pcs in c["frames"].items():
            n_frames += 1
            for p in pcs:
                n_pieces += 1
                check(len(p) == 7 and p[2] in (0, 1, 2) and 0 <= p[3] <= 3 and 0 <= p[4] <= 7 and 0 <= p[5] < 1024 and 0 <= p[6] < 16
                      and -128 <= p[0] < 128 and -128 <= p[1] < 128, "catalog sprite %s frame %s: bad piece %r" % (k, f, p))
        check(0 < len(c["frames"]) <= 256 and all(0 <= int(f) < 255 for f in c["frames"]), "catalog sprite %s: %d frames" % (k, len(c["frames"])))
        for a in c["anims"]:
            n_anims += 1
            check(1 <= len(a["f"]) <= 64 and 0 <= a["l"] <= len(a["f"]) and all(len(x) == 4 and all(0 <= v < 256 for v in x) for x in a["f"]),
                  "catalog sprite %s: bad animation" % k)
            if tables is not None:      # a frame an animation names is listed, 0xFF (draws nothing), or past the sprite's table
                ext = tables.frame_table_extent(int(k))
                unlisted += sum(1 for x in a["f"] if str(x[0]) not in c["frames"] and x[0] != 255 and not (ext is not None and x[0] >= ext))
        if "swap" in c:
            check(set(c["swap"]) == set(c["frames"]), "catalog sprite %s: swap table does not cover the listed frames" % k)
    equal(unlisted, 0, "animation frames that are neither listed, 255, nor past their sprite's frame table")
    for key, s in srcs.items():
        check(str(s["s"]) in cats, "source %s: sprite %d not in the catalog" % (key, s["s"]))
        check(s["m"] in (0, 1) and s["r"] and all(len(r) == 16 for r in s["r"]), "source %s: bad rows" % key)
        if s["m"] == 0:
            check(s["t"] in spr["tilesets"], "source %s: tileset %s missing" % (key, s["t"]))
        else:
            check("swap" in cats[str(s["s"])], "source %s: swapped but its sprite has no swap table" % key)
            check(tiles is None or str(s["s"]) in tiles["pools"], "source %s: swapped tiles missing from sprites_tiles.json.gz" % key)
    print("  %d sprites, %d frames, %d OAM pieces, %d animations, %d sources, %d fixed tile sets" %
          (len(cats), n_frames, n_pieces, n_anims, len(srcs), len(spr["tilesets"])))
    if tiles is None:
        print("  re-rendering from the catalog: skipped (no sprites_tiles.json.gz)")
        return
    equal(tiles["rom"], spr["rom"], "sprites_tiles.json.gz was built with sprites.json")
    n = 0
    for fam, lst in drawn.items():
        for key, idx, rec, pic, e, room in lst:
            n += 1
            got = rebuild_picture(spr, tiles, rec)
            check(got is not None and (got[0], got[1], got[2], got[3]) == (pic["w"], pic["h"], pic["ox"], pic["oy"])
                  and base64.b64encode(got[4]).decode("ascii") == pic["rgba"],
                  "%s %s#%s: the catalog does not reproduce the stored picture" % (fam, key, idx))
    print("  %d pictures re-rendered from the catalog alone: identical" % n)


def test_visual_sprites(maps, spr):
    print("visual: the curtain sprite of area 0x20 room 0 against the console screenshot")
    setup = screenshot_setup(maps)
    if setup is None or spr is None:
        return
    sp, placed, cmap = setup
    _, ox, oy, r, rows = placed[0]
    recs = spr["rooms"].get("20-00", {})
    cur = [(int(i), rec) for i, rec in recs.items() if rec["family"] == "OBJECT/GENTARI_CURTAIN"]
    if not check(len(cur) == 1 and "pic" in cur[0][1], "the curtain of 20-00 is drawn"):
        return
    e = r["entities"][cur[0][0]]
    pic = spr["pictures"][cur[0][1]["pic"]]
    raw = base64.b64decode(pic["rgba"])
    px0, py0 = e["x"] + pic["ox"], e["y"] + pic["oy"]
    # Pixels where the console shows something the room's layers alone do not (Link, Gentari, the curtain, the HUD...):
    # the curtain's opaque pixels must all be among them, and they must cover most of what differs inside its rectangle.
    sprite_px = both = differ = 0
    for Y in range(py0 - 4, py0 + pic["h"] + 4):
        for X in range(max(0, px0 - 4), px0 + pic["w"] + 4):
            if not (0 <= X < r["width"] and 0 <= Y < r["height"]):
                continue
            q = rows[Y][X * 3:X * 3 + 3]
            s = sp[X + ox, Y + oy]
            diff = not all(abs(cmap[c][q[c]] - s[c]) <= 2 for c in range(3))
            lx, ly = X - px0, Y - py0
            mine = 0 <= lx < pic["w"] and 0 <= ly < pic["h"] and raw[(ly * pic["w"] + lx) * 4 + 3] != 0
            sprite_px += mine
            differ += diff
            both += diff and mine
    share = both / sprite_px
    cover = both / differ
    check(share >= 0.95, "curtain: %.1f%% of its pixels are where the console differs from the layers (need 95%%)" % (share * 100))
    check(cover >= 0.70, "curtain: covers %.1f%% of what differs around it (need 70%%)" % (cover * 100))
    print("  curtain %dx%d at %d,%d: %.1f%% of its %d pixels sit on pixels the console draws differently; "
          "it explains %.1f%% of the %d differing pixels around it" % (pic["w"], pic["h"], px0, py0, share * 100, sprite_px, cover * 100, differ))


# ------------------------------------------------------------------ checksums
CHECKSUMS = {
    "EU": {
        "layers": "530bfffa5075598f",
        "entities": "31d379d56f8e4379",
        "tileEntities": "01492aa217e49234",
        "exits": "f59377de2e74c421",
        "gfx": "7dd7faf036efa825",
        "metatiles": "b13c7823c5eb8c91",
        "spriteRecords": "0a1c144002a71a25",
        "spritePictures": "d7a9e266789ee1bd",
        "spriteCatalog": "ff258a59108eb216",
        "spriteTiles": "350297beda7afc32",
    },
}


def digest(obj):
    return hashlib.sha1(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:16]


def test_checksums(maps, spr=None, tiles=None):
    print("checksums (regression)")
    got = {
        "layers": digest([(a["id"], r["id"], r["width"], r["height"], r["mode"], r["gfx"],
                           {k: (v["format"], v["w"], v["h"], v["data"], v.get("bgcnt"), v["visible"])
                            for k, v in r["layers"].items()}) for a, r in rooms_of(maps)]),
        "entities": digest([(a["id"], r["id"], [(e["raw"], e["list"], e["when"]) for e in r["entities"]])
                            for a, r in rooms_of(maps)]),
        "tileEntities": digest([(a["id"], r["id"], [t["raw"] for t in r["tileEntities"]])
                                for a, r in rooms_of(maps)]),
        "exits": digest([(a["id"], r["id"], r["exits"]) for a, r in rooms_of(maps)]),
        "gfx": digest(maps["gfx"]),
        "metatiles": digest(maps["metatiles"]),
    }
    if spr is not None:
        got["spriteRecords"] = digest(spr["rooms"])
        got["spritePictures"] = digest(spr["pictures"])
        got["spriteCatalog"] = digest([spr["sizeTable"], spr["sprites"], spr["sources"], spr["tilesets"]])
    if tiles is not None:
        got["spriteTiles"] = digest(tiles["pools"])
    want = CHECKSUMS.get(maps["rom"]["region"], {})
    changed = {k: v for k, v in got.items() if want.get(k) != v}
    for k in got:
        check(k not in changed, "checksum %s: %s (pinned %s)" % (k, got[k], want.get(k)))
    if changed:
        print("  new values, to paste into CHECKSUMS if the change is deliberate:")
        for k, v in got.items():
            print('        "%s": "%s",' % (k, v))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom", default=os.environ.get("TMC_ROM"))
    a = ap.parse_args()
    maps = load(a.rom)
    spr = load_sprites(a.rom)
    tiles = load_tiles() if spr is not None else None
    test_structure(maps)
    test_names(maps)
    if a.rom:
        rom = T.Rom(a.rom)
        equal(rom.sha1, maps["rom"]["sha1"], "maps.json was built from this ROM")
        test_label_layout(rom, maps)
        test_entities_vs_source(rom, maps)
    else:
        rom = None
        print("labels / entities vs source: skipped (no --rom / TMC_ROM)")
    test_bounds(maps)
    test_entities(maps)
    test_exits(maps)
    test_visual(maps)
    if spr is not None:
        test_sprites(rom, maps, spr, tiles)
        test_visual_sprites(maps, spr)
    else:
        print("sprites: skipped (no sprites.json and no --rom / TMC_ROM to build it)")
    test_checksums(maps, spr, tiles)
    print()
    if FAILS:
        for f in FAILS[:60]:
            print("FAIL", f)
        if len(FAILS) > 60:
            print("... and %d more" % (len(FAILS) - 60))
        print("%d of %d checks failed" % (len(FAILS), CHECKS[0]))
        return 1
    print("all %d checks passed" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
