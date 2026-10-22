#!/usr/bin/env python3
"""Writes maps.json: every room of The Minish Cap, decoded from the ROM, for the
layer workbench viewer.

    python3 tools/layer-workbench/build_maps.py --rom PATH [-o OUT]
    TMC_ROM=PATH python3 tools/layer-workbench/build_maps.py

The ROM is only read. maps.json holds the game's art (tiles, palettes, maps):
it is gitignored and must never be committed or shared. The schema is in
README.md next to this file.

Each room is loaded the way the game loads it (see tmcdata.py for the tables):
  InitRoomResInfo   (src/gameUtils.c)   header, tileset, map, tiles, exits, properties
  LoadRoomTileSet   (src/playerUtils.c) tileset gfx + palette group, metatiles, BG animations
  LoadRoomGfx       (src/playerUtils.c) room maps, BG control per area
  LoadRoom          (src/room.c)        entity lists 1, 0, (2), tile entities 3
"""
import argparse
import base64
import gzip
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tmcdata as T  # noqa: E402
import sprites as SP  # noqa: E402

FORMAT = "tmc-layer-workbench-maps"
VERSION = 1

# gUnk_080B77C0 (src/backgroundAnimations.c): BG control words.
BGCNT_BOTTOM, BGCNT_TOP, BGCNT_TOP_HOUSE = 0x1C42, 0x1D45, 0x1D46
# LoadRoomGfx's switch on the area (src/playerUtils.c). test_maps.py checks
# that the source still lists exactly these cases.
AREAS_8BPP_NO_BG1 = ("AREA_MINISH_HOUSE_INTERIORS", "AREA_TOWN_MINISH_HOLES")
AREAS_TOP_PRIO2 = ("AREA_HOUSE_INTERIORS_1", "AREA_HOUSE_INTERIORS_2", "AREA_HOUSE_INTERIORS_3",
                   "AREA_TREE_INTERIORS", "AREA_DOJOS", "AREA_MINISH_CRACKS", "AREA_HOUSE_INTERIORS_4",
                   "AREA_WIND_TRIBE_TOWER", "AREA_EZLO_CUTSCENE")

ENTITY_SIZE = 16
TILE_ENTITY_SIZE = 8
TRANSITION_SIZE = 20
MAX_LIST = 128          # no list in the game comes near this; a runaway read stops here

# Entity lists in a room's property table (LoadRoom, src/room.c).
PROPERTY_LISTS = {
    0: "always",                # LoadRoomEntityList(GetCurrentRoomProperty(0))
    1: "always",                # LoadRoomEntityList(GetCurrentRoomProperty(1)), loaded first
    2: "afterTabidachi",        # only once global flag TABIDACHI is set (sub_0804B058)
}


def b64(data):
    return base64.b64encode(bytes(data)).decode("ascii")


def u16s(data):
    return list(struct.unpack("<%dH" % (len(data) // 2), data[:len(data) // 2 * 2]))


def pack_u16(values):
    return struct.pack("<%dH" % len(values), *values)


def bgcnt_info(cnt):
    return {
        "bgcnt": cnt,
        "priority": cnt & 3,
        "charBase": (cnt >> 2) & 3,
        "colors": 256 if cnt & 0x80 else 16,
        "screenBase": (cnt >> 8) & 0x1F,
        "mosaic": bool(cnt & 0x40),
    }


class Builder:
    def __init__(self, rom):
        self.rom = rom
        self.enums = T.load_enums()
        self.area_names = self.enums["areas"]
        self.room_names = T.load_room_names(self.area_names)
        self.labels, self.structs = self.layout_labels()
        self.callbacks = T.parse_room_callbacks()
        self.bg_frames = self.bg_animation_frames()
        self.common_palette_groups = common_palette_groups()
        self.gfx = {}           # hash -> entry
        self.gfx_keys = {}      # hash -> key
        self.metatiles = {}
        self.warnings = []
        self.stats = {"specialTilesInMaps": 0}

    # ---------------------------------------------------------- labels
    def layout_labels(self):
        """entity_headers.s labels -> ROM file offsets for this region. The
        base is pinned by gAreaTable[0], which entity_headers.s starts with."""
        labels, structs = T.layout_entity_headers(self.rom.region)
        first = self.rom.table_entry(self.rom.off["areaTable"], 0)
        base = first - labels["Area_MinishWoods"]
        at = {name: base + off for name, off in labels.items()}
        self.label_at = {off: name for name, off in at.items()}
        # Labels that hold data in the other region but none in this one (an
        # .ifdef'd-out list). A label empty in both is an alias, not absent.
        other = "USA" if self.rom.region == "EU" else "EU"
        self.empty_labels = empty_labels(labels) - empty_labels(T.layout_entity_headers(other)[0])
        return at, structs

    def label_struct(self, rom_off):
        name = self.label_at.get(rom_off)
        return name, self.structs.get(name)

    # ---------------------------------------------------------- bg anims
    def bg_animation_frames(self):
        """gUnk_080B7278 (frame table) sits 0x2E4 bytes before gUnk_080B755C
        (per-area lists) in the USA ROM; the same layout is assumed for the
        region read and validated: every entry must be a ROM pointer whose
        first gfx record points into ROM too."""
        text = T.read_text("src/backgroundAnimations.c")
        m = re.search(r"gUnk_080B7278\[\]\s*=\s*\{(.*?)\};", text, re.S)
        count = len([x for x in m.group(1).split(",") if x.strip()])
        table = self.rom.off["bgAnimTable"] - (0x0B755C - 0x0B7278)
        frames = []
        for i in range(count):
            f = self.rom.read_ptr(table + 4 * i)
            g = self.rom.read_ptr(f) if f is not None else None
            if g is None:
                print("warning: BG animation frame table did not validate; animations skipped",
                      file=sys.stderr)
                return None
            frames.append(f)
        return frames

    def area_bg_animations(self, area):
        if self.bg_frames is None:
            return []
        lst = self.rom.read_ptr(self.rom.off["bgAnimTable"] + 4 * area)
        if lst is None:
            return []
        ids = []
        while True:
            v = self.rom.u16(lst)
            if v == 0xFFFF:
                return ids
            if v >= len(self.bg_frames):
                self.warnings.append("area %d: BG animation id %d past the frame table" % (area, v))
            else:
                ids.append(v)
            lst += 2

    def apply_bg_animation(self, anim_id, vram, pal, mask):
        """LoadBgAnimationGfx(frame->gfx) for the first frame: what the port
        primes at room init and the GBA shows after the first timer."""
        frame = self.bg_frames[anim_id]
        g = self.rom.read_ptr(frame)
        base = self.rom.off["gfxAndPalettes"]
        for _ in range(16):
            vofs, size, flags = struct.unpack_from("<HBB", self.rom.data, g)
            src = base + self.rom.u32(g + 4)
            if flags & 0x10:
                first = vofs >> 5
                for k in range(size * 16):
                    if (first * 16 + k) < 512:
                        pal[first * 16 + k] = self.rom.u16(src + 2 * k)
                for p in range(first, first + size):
                    mask[0] |= 1 << p
            else:
                n = size << 5
                vram[vofs:vofs + n] = self.rom.data[src:src + n]
            if not flags & 0x80:
                return
            g += 8

    # ---------------------------------------------------------- gfx
    def load_palette_group(self, group, pal, mask, groups, copy_obj2=True):
        for dest, colors, _ in self.rom.palette_group(group):
            for k, c in enumerate(colors):
                if dest * 16 + k < 512:
                    pal[dest * 16 + k] = c
            for p in range(dest, dest + len(colors) // 16):
                mask[0] |= 1 << p
        groups.append(group)
        if copy_obj2:
            # LoadMapData follows every palette group with sub_080533CC
            # (src/gameUtils.c): BG palette 2 colours 0-4 <- OBJ palette 2.
            pal[32:37] = pal[288:293]

    def apply_definitions(self, defs, vram, pal, mask, groups, sources):
        """The VRAM and palette parts of a MapDataDefinition chain."""
        for d in defs:
            if d["dest"] == 0:
                self.load_palette_group(d["src"] & 0xFFFF, pal, mask, groups)
                continue
            if T.VRAM <= d["dest"] < T.VRAM + T.BG_VRAM_SIZE:
                data = self.rom.load_map_data(d)
                o = d["dest"] - T.VRAM
                if o + len(data) > T.BG_VRAM_SIZE:
                    self.warnings.append("VRAM load past BG VRAM at 0x%X" % d["dest"])
                    data = data[:T.BG_VRAM_SIZE - o]
                vram[o:o + len(data)] = data
                sources.append({"vram": o, "size": len(data)})

    def gfx_key(self, vram, pal, mask, groups, sources, anims):
        h = hashlib.sha1(bytes(vram) + pack_u16(pal) + struct.pack("<I", mask)).hexdigest()
        if h not in self.gfx_keys:
            key = "g%d" % len(self.gfx_keys)
            self.gfx_keys[h] = key
            self.gfx[key] = {
                "vram": b64(vram),
                "palettes": b64(pack_u16(pal)),
                "paletteLoaded": mask,
                "paletteGroups": groups,
                "vramLoads": sources,
                "bgAnimations": anims,
            }
        return self.gfx_keys[h]

    # ---------------------------------------------------------- metatiles
    def area_metatiles(self, area):
        key = "%d" % area
        if key in self.metatiles:
            return key
        tiles = self.rom.read_ptr(self.rom.off["areaTiles"] + 4 * area)
        entry = {}
        names = {
            T.GMAP_BOTTOM + T.MAPLAYER_SUBTILES: "bottom",
            T.GMAP_TOP + T.MAPLAYER_SUBTILES: "top",
            T.GMAP_BOTTOM + T.MAPLAYER_TILETYPES: "bottomTypes",
            T.GMAP_TOP + T.MAPLAYER_TILETYPES: "topTypes",
        }
        if tiles is not None:
            for d in self.rom.map_definitions(tiles):
                n = names.get(d["dest"])
                if n is None:
                    self.warnings.append("area %d tiles: unexpected dest 0x%X" % (area, d["dest"]))
                    continue
                entry[n] = b64(self.rom.load_map_data(d))
        self.metatiles[key] = entry
        return key

    def metatile_sets(self, area):
        return self.metatiles[self.area_metatiles(area)]

    # ---------------------------------------------------------- entities
    def decode_entity_list(self, off, source):
        out = []
        for i in range(MAX_LIST):
            o = off + i * ENTITY_SIZE
            kind_b, flags, eid, etype = self.rom.data[o:o + 4]
            if kind_b == 0xFF:
                return out, True
            type2, x, y, paramc = struct.unpack_from("<IHHI", self.rom.data, o + 4)
            kind = kind_b & 0xF
            kind_name = self.enums["entityKinds"].get(kind)
            spawn = flags >> 4
            collision = kind_b >> 4
            e = {
                "list": source,
                "index": i,
                "kind": kind,
                "kindName": kind_name,
                "id": eid,
                "idName": self.enums["ids"].get(kind_name, {}).get(eid) if kind_name else None,
                "type": etype,
                "type2": type2 & 0xFF,
                "timer": (type2 >> 8) & 0xFF,
                "paramB": type2,
                "x": x,
                "y": y,
                "paramC": paramc,
                "collision": collision,
                "layer": 1 if collision & 1 else 2 if collision & 2 else None,
                "pool": flags & 0xF,
                "spawn": spawn,
                "positioned": kind != 9 and spawn in (0, 2, 4),
                "raw": self.rom.data[o:o + ENTITY_SIZE].hex(),
            }
            out.append(e)
        return out, False

    def decode_tile_entities(self, off):
        out = []
        for i in range(MAX_LIST):
            o = off + i * TILE_ENTITY_SIZE
            t, flag, b2, b3, pos, b6, b7 = struct.unpack_from("<BBBBHBB", self.rom.data, o)
            if t == 0:
                return out, True
            name = self.enums["tileEntityTypes"].get(t)
            if name in ("SMALL_CHEST", "DESTRUCTIBLE_TILE"):
                # tilePos is a metatile index in the 64-wide layer maps
                # (LoadSmallChestTile, LoadDestructibleTile -> SetTileType).
                x, y = (pos & 0x3F) * 16, (pos >> 6) * 16
            elif name in ("BIG_CHEST", "BOMBABLE_WALL"):
                # tilePos is the x pixel, bytes 6-7 the y pixel (LoadBombableWallTile;
                # the dungeon map's chest marks in src/common.c).
                x, y = pos, b6 | (b7 << 8)
            else:
                x = y = None
            out.append({
                "index": i, "type": t, "typeName": name,
                "localFlag": flag, "b2": b2, "b3": b3, "tilePos": pos,
                "x": x, "y": y, "b6": b6, "b7": b7, "raw": self.rom.data[o:o + 8].hex(),
            })
        return out, False

    def room_entities(self, area, room, where):
        """Entity lists and tile entities of one room, plus the lists its
        callbacks in src/roomInit.c load."""
        props = self.rom.table_entry(self.rom.table_entry(self.rom.off["areaTable"], area), room) \
            if self.rom.table_entry(self.rom.off["areaTable"], area) is not None else None
        result = {"properties": None, "entities": [], "tileEntities": [], "callbacks": [],
                  "unresolvedLists": []}
        if props is None:
            return result
        label, slots = self.label_struct(props)
        result["properties"] = label
        nslots = len(slots) if slots else 8
        callbacks = []
        list_addrs = []
        for i in range(nslots):
            p = self.rom.read_ptr(props + 4 * i)
            sym = slots[i] if slots and i < len(slots) else None
            if i in PROPERTY_LISTS and p is not None:
                list_addrs.append(p)
                ents, ok = self.decode_entity_list(p, "property%d" % i)
                for e in ents:
                    e["when"] = PROPERTY_LISTS[i]
                    e["label"] = self.label_at.get(p)
                result["entities"] += ents
                if not ok:
                    self.warnings.append("%s: property %d list without terminator" % (where, i))
            elif i == 3 and p is not None:
                te, ok = self.decode_tile_entities(p)
                result["tileEntities"] = te
                if not ok:
                    self.warnings.append("%s: tile entities without terminator" % where)
            elif 4 <= i <= 7 and isinstance(sym, str):
                callbacks.append(sym)
        result["callbacks"] = callbacks
        # Lists loaded by the room's callbacks (state change, init...).
        seen = set(list_addrs)
        for fn in callbacks:
            for ref in self.callbacks.get(fn, []):
                if "property" in ref:
                    n = ref["property"]
                    p = self.rom.read_ptr(props + 4 * n) if n < nslots else None
                    src_label = self.label_at.get(p) if p is not None else None
                    key = ("prop", n)
                else:
                    src_label = ref["label"]
                    p = self.labels.get(src_label)
                    key = ("label", src_label)
                if p is None:
                    result["unresolvedLists"].append({"loadedBy": fn, "ref": ref})
                    continue
                if src_label is not None and src_label in self.empty_labels:
                    # The label holds no data in this region (an .ifdef'd-out
                    # list, e.g. gUnk_080F5B3C in EU): nothing to load.
                    result["unresolvedLists"].append({"loadedBy": fn, "ref": ref, "label": src_label,
                                                      "reason": "label empty in this region"})
                    continue
                if p in seen:
                    continue      # already decoded (as a property list or by another call)
                seen.add(p)
                if ref.get("single") or ref.get("indexed"):
                    # LoadRoomEntity(&list[i]): one record, index decided at runtime.
                    result["unresolvedLists"].append({"loadedBy": fn, "ref": ref, "label": src_label})
                    continue
                ents, ok = self.decode_entity_list(p, "callback")
                if not ok:
                    result["unresolvedLists"].append({"loadedBy": fn, "ref": ref, "label": src_label,
                                                      "reason": "no terminator"})
                    continue
                for e in ents:
                    e["when"] = "conditional"
                    e["loadedBy"] = fn
                    e["label"] = src_label
                result["entities"] += ents
        return result

    def room_exits(self, area, room):
        table = self.rom.read_ptr(self.rom.off["exitLists"] + 4 * area)
        p = self.rom.table_entry(table, room) if table is not None else None
        out = []
        if p is None:
            return out
        for i in range(MAX_LIST):
            o = p + i * TRANSITION_SIZE
            (warp, sx, sy, ex, ey, shape, darea, droom, layer, ttype, facing, sfx, u2, u3) = \
                struct.unpack_from("<5H6BHBB", self.rom.data, o)
            if warp == 0xFFFF:
                return out
            out.append({
                "warpType": warp, "warpTypeName": self.enums["warpTypes"].get(warp),
                "x": sx, "y": sy, "destX": ex, "destY": ey, "shape": shape,
                "destArea": darea, "destRoom": droom, "layer": layer,
                "transitionType": ttype, "transitionTypeName": self.enums["transitionTypes"].get(ttype),
                "facing": facing, "sfx": sfx, "unk2": u2, "unk3": u3,
            })
        self.warnings.append("area %d room %d: exit list without terminator" % (area, room))
        return out

    # ---------------------------------------------------------- rooms
    def expand_special(self, data, width, height):
        """gMapData*Special after sub_0807C5F4 (src/playerUtils.c): the source
        is a run of 32x32-entry chunks, placed left-top, right-top (width >
        255), left-bottom (height > 255), right-bottom (both), in a buffer 128
        entries wide. Returns the room's part, ceil(w/8) x ceil(h/8)."""
        src = u16s(data)
        full = [0] * (128 * 64)
        chunks = [(0, 0)]
        if width > 0xFF:
            chunks.append((32, 0))
        if height > 0xFF:
            chunks.append((0, 32))
        if width > 0xFF and height > 0xFF:
            chunks.append((32, 32))
        k = 0
        for cx, cy in chunks:
            for y in range(32):
                for x in range(32):
                    full[(cy + y) * 128 + cx + x] = src[k] if k < len(src) else 0
                    k += 1
        w8, h8 = (width + 7) // 8, (height + 7) // 8
        return [full[y * 128 + x] for y in range(h8) for x in range(w8)], w8, h8

    def build_room(self, area, room, hdr):
        map_x, map_y, width, height, ts_id = hdr
        where = "area 0x%02X room %d" % (area, room)
        ts_table = self.rom.read_ptr(self.rom.off["areaTileSets"] + 4 * area)
        ts = self.rom.table_entry(ts_table, ts_id) if ts_table is not None else None
        mp_table = self.rom.read_ptr(self.rom.off["areaRoomMaps"] + 4 * area)
        mp = self.rom.table_entry(mp_table, room) if mp_table is not None else None
        if ts is None or mp is None:
            return None, ", ".join(w for w, p in (("no tileset", ts), ("no room map", mp)) if p is None)

        vram = bytearray(T.BG_VRAM_SIZE)
        pal = [0] * 512
        mask = [0]
        groups, sources = [], []
        # LoadGfxGroups (src/gameUtils.c), run when the game task starts: the
        # palette groups every area shares (BG 0-4 and 15, OBJ 0-4), then
        # SetColor(0, 0), the black backdrop. Its gfx groups go to VRAM
        # 0xC000 and up, out of reach of BG1 and BG2, and are not loaded here.
        for g in self.common_palette_groups:
            self.load_palette_group(g, pal, mask, groups, copy_obj2=False)
        pal[0] = 0
        self.apply_definitions(self.rom.map_definitions(ts), vram, pal, mask, groups, sources)
        anims = self.area_bg_animations(area)
        for a in anims:
            self.apply_bg_animation(a, vram, pal, mask)
        room_defs = self.rom.map_definitions(mp)
        self.apply_definitions(room_defs, vram, pal, mask, groups, sources)

        buffers = {}
        for d in room_defs:
            n = T.DEST_NAMES.get(d["dest"])
            if n:
                buffers[n] = self.rom.load_map_data(d)
            elif not (d["dest"] == 0 or T.VRAM <= d["dest"] < T.VRAM + T.BG_VRAM_SIZE):
                self.warnings.append("%s: map data to unknown dest 0x%X" % (where, d["dest"]))

        tw, th = width // 16, height // 16
        bottom = buffers.get("bottom.mapData", b"")
        special = len(bottom) >= 2 and u16s(bottom[:2])[0] == 0xFFFF
        area_name = self.area_names.get(area, "AREA_0x%02X" % area)

        bg2_cnt, bg1_cnt = BGCNT_BOTTOM, BGCNT_TOP
        bg1_visible = True
        if area_name in AREAS_8BPP_NO_BG1:
            bg2_cnt |= 0x80
            bg1_visible = False
        elif area_name in AREAS_TOP_PRIO2:
            bg1_cnt = BGCNT_TOP_HOUSE

        layers = {}
        for bg, cnt, src, spc, visible in (("BG2", bg2_cnt, "bottom", "bottomSpecial", True),
                                            ("BG1", bg1_cnt, "top", "topSpecial", bg1_visible)):
            layer = {"source": "gMap" + src.capitalize(), "visible": visible}
            layer.update(bgcnt_info(cnt))
            if special:
                data, w8, h8 = self.expand_special(buffers.get(spc, b""), width, height)
                layer.update({"format": "tilemap", "w": w8, "h": h8, "data": b64(pack_u16(data)),
                              "loaded": spc in buffers})
            else:
                vals = u16s(buffers.get(src + ".mapData", b""))
                vals = (vals + [0] * (tw * th))[:tw * th]
                self.stats["specialTilesInMaps"] += sum(1 for v in vals if v >= 0x4000)
                layer.update({"format": "metatile", "metatileSet": src, "w": tw, "h": th,
                              "data": b64(pack_u16(vals)), "loaded": (src + ".mapData") in buffers})
            # What the room leaves undefined: LoadRoomGfx clears gMapTop.mapData
            # and both special buffers, but not gMapBottom.mapData; and an area
            # without a metatile set keeps the previous area's in gMapTop/Bottom.
            reason = None
            if not special and not layer["loaded"] and src == "bottom":
                reason = "gMapBottom.mapData is not loaded by this room (keeps the previous room's)"
            elif not special and src not in self.metatile_sets(area):
                reason = "the area has no %s metatile set (gMap%s keeps the previous area's)" % (src, src.capitalize())
            layer["defined"] = reason is None
            if reason:
                layer["visible"] = False
                layer["note"] = reason
            layers[bg] = layer
        # BG3: some tilesets fill screen block 30 (VRAM 0xF000), BG3's map. It
        # is not shown by LoadRoomGfx (DISPCNT leaves BG3 off); room managers
        # turn it on and set its control at runtime.
        if any(s["vram"] <= 0xF000 < s["vram"] + s["size"] for s in sources):
            layers["BG3"] = {"source": "tileset VRAM screen block 30", "visible": False,
                             "format": "tilemap", "w": 32, "h": 32, "bgcnt": None,
                             "data": b64(vram[0xF000:0xF800]), "loaded": True, "defined": True,
                             "note": "BG3 is enabled and configured by room managers at runtime"}
        order = sorted([bg for bg, l in layers.items() if l["visible"]],
                       key=lambda bg: (-layers[bg]["priority"], -int(bg[2])))

        ents = self.room_entities(area, room, where)
        names = self.room_names.get(area, {})
        enum_name = names.get(room)
        prefix = "ROOM_" + area_name[len("AREA_"):] + "_"
        rec = {
            "id": room,
            "name": enum_name[len(prefix):] if enum_name and enum_name.startswith(prefix) else enum_name,
            "enumName": enum_name,
            "worldX": map_x, "worldY": map_y,
            "width": width, "height": height,
            "tileSetId": ts_id,
            "gfx": self.gfx_key(vram, pal, mask[0], groups, sources, anims),
            "metatiles": self.area_metatiles(area),
            "mode": "special" if special else "metatile",
            "layers": layers,
            "drawOrder": order,
            "properties": ents["properties"],
            "callbacks": ents["callbacks"],
            "entities": ents["entities"],
            "tileEntities": ents["tileEntities"],
            "unresolvedLists": ents["unresolvedLists"],
            "exits": self.room_exits(area, room),
        }
        return rec, None

    def build(self):
        areas, skipped = [], []
        for area in range(self.rom.off["areaTileSetsCount"]):
            hdrs = self.rom.read_ptr(self.rom.off["areaRoomHeaders"] + 4 * area)
            if hdrs is None:
                continue
            rooms = []
            o, room = hdrs, 0
            while self.rom.u16(o) != 0xFFFF and room < T_MAX_ROOMS:
                hdr = struct.unpack_from("<5H", self.rom.data, o)
                o += 10
                if hdr[2] == 0 or hdr[3] == 0 or hdr[4] >= 0xFF00:
                    room += 1
                    continue      # placeholder header (all zero, tileSet 0xFFxx)
                rec, why = self.build_room(area, room, hdr)
                if rec is None:
                    skipped.append({"area": area, "room": room, "reason": why})
                else:
                    rooms.append(rec)
                room += 1
            if rooms:
                areas.append({"id": area, "name": self.area_names.get(area), "rooms": rooms})
        return areas, skipped


T_MAX_ROOMS = 64    # MAX_ROOMS (include/area.h)


def empty_labels(labels):
    ordered = sorted(labels.items(), key=lambda kv: kv[1])
    return {n for (n, o), (_, o2) in zip(ordered, ordered[1:]) if o == o2}


def common_palette_groups():
    """The LoadPaletteGroup(n) calls of LoadGfxGroups (src/gameUtils.c)."""
    text = T.strip_c_comments(T.read_text("src/gameUtils.c"))
    m = re.search(r"void LoadGfxGroups\(void\)\s*\{(.*?)\n\}", text, re.S)
    return [int(x, 0) for x in re.findall(r"LoadPaletteGroup\((\w+)\)", m.group(1))]


def write_sprites(rom, builder, areas, path):
    """sprites.json: the pictures of the entities sprites.py knows how to draw."""
    gfx_sets = {k: {"palettes": u16s(base64.b64decode(g["palettes"])), "mask": g["paletteLoaded"]}
                for k, g in builder.gfx.items()}
    sb = SP.SpriteBuilder(rom, builder.enums)
    sb.build(areas, gfx_sets)
    text = json.dumps(sb.result(), separators=(",", ":"))
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    tiles = json.dumps(sb.tiles_result(), separators=(",", ":"))
    with gzip.open(os.path.join(os.path.dirname(path), "sprites_tiles.json.gz"), "wt", encoding="utf-8") as f:
        f.write(tiles)
    st = sb.stats
    print("%s: %d curated sprites drawn (%s), %d skipped; %d default sprites drawn (%s), %d skipped; %d pictures, "
          "%d sprites in the catalog, %.2f MB (+ %.2f MB of swapped tiles in sprites_tiles.json.gz, loaded on demand)"
          % (os.path.basename(path), st["drawn"], ", ".join("%s %d" % kv for kv in sorted(st["byFamily"].items())),
             st["skipped"], st["defaultDrawn"], ", ".join("%s %d" % kv for kv in sorted(st["defaultByKind"].items())),
             st["defaultSkipped"], len(sb.pictures), len(sb.sprite_cat), len(text) / 1048576, len(tiles) / 1048576))
    for k, n in sorted(sb.skips.items()):
        print("  not drawn x%d: %s" % (n, k))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom", default=os.environ.get("TMC_ROM"),
                    help="the retail ROM (USA or EU); default: $TMC_ROM")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "maps.json"))
    ap.add_argument("--no-gzip", action="store_true", help="do not also write maps.json.gz")
    ap.add_argument("--no-sprites", action="store_true", help="do not write sprites.json next to the output")
    a = ap.parse_args(argv)
    if not a.rom:
        ap.error("pass --rom PATH or set TMC_ROM")
    rom = T.Rom(a.rom)
    if not rom.known_dump:
        print("warning: ROM sha1 %s is not the known %s dump" % (rom.sha1, rom.region), file=sys.stderr)
    b = Builder(rom)
    areas, skipped = b.build()
    out = {
        "format": FORMAT,
        "version": VERSION,
        "rom": {"region": rom.region, "gameCode": rom.game_code, "sha1": rom.sha1},
        "enums": {k: ({str(i): n for i, n in v.items()} if k != "ids" else
                      {kk: {str(i): n for i, n in vv.items()} for kk, vv in v.items()})
                  for k, v in b.enums.items()},
        "gfx": b.gfx,
        "metatiles": b.metatiles,
        "areas": areas,
        "skippedRooms": skipped,
        "warnings": b.warnings,
        "stats": b.stats,
    }
    text = json.dumps(out, separators=(",", ":"))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(text)
    if not a.no_gzip:
        with gzip.open(a.out + ".gz", "wt", encoding="utf-8") as f:
            f.write(text)
    if not a.no_sprites:
        write_sprites(rom, b, areas, os.path.join(os.path.dirname(os.path.abspath(a.out)), "sprites.json"))
    nrooms = sum(len(x["rooms"]) for x in areas)
    nents = sum(len(r["entities"]) for x in areas for r in x["rooms"])
    print("%s: %d areas, %d rooms, %d gfx sets, %d entities, %d skipped rooms, %d warnings, %.1f MB"
          % (os.path.basename(a.out), len(areas), nrooms, len(b.gfx), nents, len(skipped),
             len(b.warnings), len(text) / 1048576))
    for w in b.warnings[:20]:
        print("  warning:", w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
