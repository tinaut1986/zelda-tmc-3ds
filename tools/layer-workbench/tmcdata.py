"""Readers shared by build_maps.py and test_maps.py.

Two halves:

- The ROM. Rooms are read the way the game reads them, by walking the same
  pointer tables from the retail ROM (gAreaRoomHeaders, gAreaTileSets,
  gAreaRoomMaps, gAreaTiles, gAreaTable, gExitLists). Their offsets per region
  are parsed from port/port_rom.c (kRomOffsets_USA / kRomOffsets_EU), the same
  numbers the port uses, so there is no second copy that could drift.
- The repository. Names (areas, rooms, entity ids, tile entity types) come from
  the enums in include/; the labels of entity lists come from
  data/map/entity_headers.s, laid out byte by byte so each label gets its ROM
  address in the region being read; the lists that rooms load conditionally
  come from the callbacks in src/roomInit.c.

Nothing here writes anything; the ROM is only read.
"""
import hashlib
import json
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

ROM_BASE = 0x08000000

KNOWN_ROMS = {
    "b4bd50e4131b027c334547b4524e2dbbd4227130": "USA",
    "cff199b36ff173fb6faf152653d1bccf87c26fb7": "EU",
}
GAME_CODES = {"BZME": "USA", "BZMP": "EU"}

# MapDataDefinition (include/map.h): {u32 src, u32 dest, u32 size}.
MAP_MULTIPLE = 0x80000000      # in src: another definition follows
MAP_COMPRESSED = 0x80000000    # in size: src is LZ77

# GBA addresses of the buffers room data is loaded into. They are the same in
# USA and EU (port/port_rom.c, Port_ResolveEwramPtr).
GMAP_BOTTOM = 0x02025EB0       # MapLayer gMapBottom
GMAP_TOP = 0x0200B650          # MapLayer gMapTop
MAPLAYER_MAPDATA = 0x0004      # MapLayer.mapData (include/map.h)
MAPLAYER_COLLISION = 0x2004
MAPLAYER_TILETYPES = 0x5004
MAPLAYER_SUBTILES = 0x7004
MAPDATA_BOTTOM_SPECIAL = 0x02019EE0
MAPDATA_TOP_SPECIAL = 0x02002F00
VRAM = 0x06000000
BG_VRAM_SIZE = 0x10000         # BG character + screen blocks (0x06000000-0x0600FFFF)

DEST_NAMES = {
    GMAP_BOTTOM + MAPLAYER_MAPDATA: "bottom.mapData",
    GMAP_TOP + MAPLAYER_MAPDATA: "top.mapData",
    GMAP_BOTTOM + MAPLAYER_COLLISION: "bottom.collisionData",
    GMAP_TOP + MAPLAYER_COLLISION: "top.collisionData",
    GMAP_BOTTOM + MAPLAYER_TILETYPES: "bottom.tileTypes",
    GMAP_TOP + MAPLAYER_TILETYPES: "top.tileTypes",
    GMAP_BOTTOM + MAPLAYER_SUBTILES: "bottom.subTiles",
    GMAP_TOP + MAPLAYER_SUBTILES: "top.subTiles",
    MAPDATA_BOTTOM_SPECIAL: "bottomSpecial",
    MAPDATA_TOP_SPECIAL: "topSpecial",
}


def read_text(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


# ------------------------------------------------------------------ LZ77
def lz77_decompress(data, off):
    """GBA BIOS LZ77UnComp (GBATEK), as LoadMapData calls it. Returns
    (bytes, compressed length consumed)."""
    if data[off] != 0x10:
        raise ValueError("not LZ77 at 0x%X: 0x%02X" % (off, data[off]))
    size = data[off + 1] | (data[off + 2] << 8) | (data[off + 3] << 16)
    out = bytearray()
    i = off + 4
    while len(out) < size:
        flags = data[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b0, b1 = data[i], data[i + 1]
                i += 2
                length = (b0 >> 4) + 3
                disp = (((b0 & 0x0F) << 8) | b1) + 1
                start = len(out) - disp
                if start < 0:
                    raise ValueError("LZ77 back-reference before start at 0x%X" % off)
                for k in range(length):
                    out.append(out[start + k])
            else:
                out.append(data[i])
                i += 1
    return bytes(out[:size]), i - off


# ------------------------------------------------------------------ ROM
def parse_rom_offsets():
    """kRomOffsets_<REGION> from port/port_rom.c -> {region: {field: int}}."""
    text = strip_c_comments(read_text("port/port_rom.c"))
    out = {}
    for m in re.finditer(r"const RomOffsets kRomOffsets_(\w+)\s*=\s*\{(.*?)\};", text, re.S):
        fields = {}
        for f in re.finditer(r"\.(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)\s*,", m.group(2)):
            fields[f.group(1)] = int(f.group(2), 0)
        out[m.group(1)] = fields
    return out


class Rom:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        self.sha1 = hashlib.sha1(self.data).hexdigest()
        self.game_code = self.data[0xAC:0xB0].decode("ascii", "replace")
        self.region = GAME_CODES.get(self.game_code)
        if self.region is None:
            raise SystemExit("unsupported ROM (game code %r); use the USA (BZME) or EU (BZMP) ROM"
                             % self.game_code)
        self.known_dump = KNOWN_ROMS.get(self.sha1) == self.region
        offsets = parse_rom_offsets()
        if self.region not in offsets:
            raise SystemExit("port/port_rom.c has no kRomOffsets_%s" % self.region)
        self.off = offsets[self.region]

    def u8(self, o):
        return self.data[o]

    def u16(self, o):
        return struct.unpack_from("<H", self.data, o)[0]

    def u32(self, o):
        return struct.unpack_from("<I", self.data, o)[0]

    def ptr(self, value):
        """A GBA ROM address -> file offset, or None (NULL, RAM, out of range)."""
        value &= ~1
        if ROM_BASE <= value < ROM_BASE + len(self.data):
            return value - ROM_BASE
        return None

    def read_ptr(self, o):
        return self.ptr(self.u32(o))

    def table_entry(self, table_off, index):
        """table[index] for a table of ROM pointers at file offset table_off."""
        if table_off is None:
            return None
        return self.read_ptr(table_off + 4 * index)

    def lz77(self, o):
        return lz77_decompress(self.data, o)[0]

    def map_definitions(self, o):
        """A MapDataDefinition chain (LoadMapData, src/beanstalkSubtask.c) ->
        list of dicts. src is an offset from gMapData; dest 0 means
        "load palette group src & 0xFFFF"."""
        out = []
        for _ in range(64):
            src, dest, size = struct.unpack_from("<3I", self.data, o)
            out.append({"src": src & 0x7FFFFFFF, "dest": dest, "size": size & 0x7FFFFFFF,
                        "compressed": bool(size & MAP_COMPRESSED), "at": o})
            o += 12
            if not src & MAP_MULTIPLE:
                return out
        raise ValueError("MapDataDefinition chain without terminator")

    def load_map_data(self, d):
        """The bytes one definition copies to its dest."""
        src = self.off["mapDataBase"] + d["src"]
        if d["compressed"]:
            return self.lz77(src)
        return self.data[src:src + d["size"]]

    def palette_group(self, group):
        """LoadPaletteGroup (src/common.c) -> list of (destPalette, colors u16[16*n])."""
        table = self.off["paletteGroups"]
        count = self.off["paletteGroupsCount"]
        if group >= count:
            raise ValueError("palette group %d out of range (%d)" % (group, count))
        o = self.read_ptr(table + 4 * group)
        base = self.off["gfxAndPalettes"]
        out = []
        while True:
            pg = self.u32(o)
            dest = (pg >> 16) & 0xFF
            n = (pg >> 24) & 0xF or 16
            src = base + (pg & 0xFFFF) * 32
            out.append((dest, list(struct.unpack_from("<%dH" % (16 * n), self.data, src)), pg & 0xFFFF))
            if not (pg >> 24) & 0x80:
                return out
            o += 4


# ------------------------------------------------------------------ enums
def parse_enum_body(body):
    """Enumerators of a C enum body -> {value: name}; handles `= value`."""
    out = {}
    nxt = 0
    for item in strip_c_comments(body).split(","):
        item = item.strip()
        if not item:
            continue
        if item.startswith("#"):
            raise ValueError("preprocessor line inside enum: %r" % item)
        if "=" in item:
            name, value = [s.strip() for s in item.split("=", 1)]
            nxt = int(value, 0)
        else:
            name = item
        if not re.match(r"^[A-Za-z_]\w*$", name):
            raise ValueError("unexpected enumerator %r" % name)
        out.setdefault(nxt, name)
        nxt += 1
    return out


def enum_by_closing_name(rel, name):
    """`typedef enum { ... } Name;`"""
    text = read_text(rel)
    m = re.search(r"typedef enum\s*\{([^{}]*)\}\s*" + name + r"\s*;", text, re.S)
    if not m:
        raise ValueError("%s: no enum %s" % (rel, name))
    return parse_enum_body(m.group(1))


def enum_by_first(rel, first):
    """The enum whose first enumerator is `first` (for anonymous enums)."""
    text = read_text(rel)
    for m in re.finditer(r"enum\s*\w*\s*\{([^{}]*)\}", text, re.S):
        body = parse_enum_body(m.group(1))
        if body and body.get(min(body)) == first:
            return body
    raise ValueError("%s: no enum starting with %s" % (rel, first))


def load_enums():
    kinds = enum_by_closing_name("include/entity.h", "EntityKind")
    ids = {
        "ENEMY": enum_by_first("include/enemy.h", "OCTOROK"),
        "OBJECT": enum_by_closing_name("include/object.h", "Object"),
        "NPC": enum_by_first("include/npc.h", "NPC_NONE_0"),
        "MANAGER": enum_by_closing_name("include/manager.h", "Managers"),
        "PROJECTILE": enum_by_closing_name("include/projectile.h", "Projectile"),
        "PLAYER_ITEM": enum_by_closing_name("include/playeritem.h", "PlayerItem"),
    }
    return {
        "entityKinds": kinds,
        "ids": ids,
        "tileEntityTypes": enum_by_closing_name("include/room.h", "TileEntityType"),
        "areas": enum_by_closing_name("include/area.h", "AreaID"),
        "warpTypes": enum_by_closing_name("include/transitions.h", "WarpType"),
        "transitionTypes": enum_by_closing_name("include/transitions.h", "TransitionType"),
    }


def enum_hex_comments(rel, first):
    """`/*0x5e*/ CURTAIN,` comments of an enum -> {name: value}, to cross-check
    the counting in parse_enum_body."""
    text = read_text(rel)
    for m in re.finditer(r"enum\s*\w*\s*\{([^{}]*)\}", text, re.S):
        if re.search(r"\b" + first + r"\b", m.group(1).split(",")[0]):
            return {n: int(v, 16) for v, n in re.findall(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*(\w+)", m.group(1))}
    return {}


def load_room_names(areas):
    """include/roomid.h: one enum, sections headed `// AREA_X`, each restarting
    at 0. -> {area_id: {room_id: enumerator}}."""
    text = read_text("include/roomid.h")
    m = re.search(r"typedef enum\s*\{(.*?)\}\s*RoomID\s*;", text, re.S)
    body = m.group(1) if m else text
    by_name = {v: k for k, v in areas.items()}
    out = {}
    area = None
    nxt = 0
    for line in body.split("\n"):
        line = line.strip()
        c = re.match(r"//\s*(AREA_\w+)", line)
        if c:
            area = by_name.get(c.group(1))
            nxt = 0
            continue
        line = strip_c_comments(line).strip().rstrip(",").strip()
        if not line or area is None:
            continue
        if "=" in line:
            name, value = [s.strip() for s in line.split("=", 1)]
            nxt = int(value, 0)
        else:
            name = line
        out.setdefault(area, {})[nxt] = name
        nxt += 1
    return out


# ------------------------------------------------------ entity_headers.s
ENTITY_MACROS_16 = {
    "entity_raw", "object_raw", "enemy_raw", "npc_raw", "manager", "projectile_raw",
    "object_item", "enemy_tree_item", "enemy_chuchu", "enemy_octorok", "object_minish_lilypad",
    "entity_list_end", "ezlo_hint", "delayed_entity_raw",
}
FIXED_SIZES = {
    "tile_entity": 8, "tile_entity_list_end": 8, "exit_raw": 20, "exit_region_raw": 8,
    "exit_region_list_end": 8, "minish_entrance": 8,
}
REGION_SYMBOLS = {
    # The helper labels entity_headers.s defines at its top for each region.
    "EU": {"EU", "EU_JP"},
    "USA": {"USA", "USA_DEMOS"},
}


def _count_items(args):
    args = args.strip()
    return len([a for a in args.split(",") if a.strip()]) if args else 0


def layout_entity_headers(region, records=None):
    """Lay data/map/entity_headers.s out byte by byte for `region`.

    Returns (labels, rooms):
      labels: {label: byte offset from the first byte of the file's data}
      rooms:  {label: [symbol or int, ...]}, the .4byte items under each
              label (a room's property table is the 4-byte slots of its label)
    The base address is fixed by the caller against a table the ROM itself
    holds (gAreaTable), and every Area_/Room_ label is then checked against the
    ROM, so a wrong size here cannot go unnoticed.
    If `records` is a list, every data directive is appended to it as
    (label, offset, directive, argument text): test_maps.py compares the
    entity macros with what the decoder reads from the ROM.
    """
    text = read_text("data/map/entity_headers.s")
    defined = REGION_SYMBOLS[region]
    assets = {}
    for e in json.loads(read_text("assets/assets.json")):
        if "path" in e and "size" in e:
            assets[e["path"]] = e["size"]
    labels = {}
    rooms = {}
    cond = []          # stack of booleans
    off = 0
    current = None
    for raw in text.split("\n"):
        line = raw.split("@", 1)[0].strip() if not raw.strip().startswith(".incbin") else raw.strip()
        line = re.sub(r"/\*.*?\*/", "", line).strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith(".ifdef") or line.startswith(".ifndef"):
            sym = line.split()[1]
            val = sym in defined
            cond.append(val if line.startswith(".ifdef") else not val)
            continue
        if line.startswith(".else"):
            cond[-1] = not cond[-1]
            continue
        if line.startswith(".endif"):
            cond.pop()
            continue
        if not all(cond):
            continue
        m = re.match(r"^(\w+)::?\s*(.*)$", line)
        if m and not line.startswith("."):
            name = m.group(1)
            if name not in ("EU_JP", "USA_DEMOS"):
                labels[name] = off
                current = name
                rooms[name] = []
            line = m.group(2).strip()
            if not line:
                continue
        word = line.split()[0]
        args = line[len(word):]
        if word in (".include", ".section", ".align"):
            continue
        if records is not None:
            records.append((current, off, word, args.strip()))
        if word in ENTITY_MACROS_16:
            off += 16
        elif word in FIXED_SIZES:
            off += FIXED_SIZES[word]
        elif word == ".4byte":
            items = [a.strip() for a in args.split(",") if a.strip()]
            if current in rooms:
                for a in items:
                    rooms[current].append(int(a, 0) if re.match(r"^(0x[0-9A-Fa-f]+|\d+)$", a) else a)
            off += 4 * len(items)
        elif word == ".2byte":
            off += 2 * _count_items(args)
        elif word == ".byte":
            off += _count_items(args)
        elif word == ".space":
            off += int(args.strip(), 0)
        elif word == ".incbin":
            path = re.search(r'"([^"]+)"', args).group(1)
            if path not in assets:
                raise ValueError("entity_headers.s: no size for .incbin %s in assets/assets.json" % path)
            off += assets[path]
        else:
            raise ValueError("entity_headers.s: unknown directive %r" % line)
    if cond:
        raise ValueError("entity_headers.s: unbalanced .if")
    return labels, rooms


def parse_room_callbacks():
    """src/roomInit.c: {function: [labels its body passes to LoadRoomEntityList
    or LoadRoomEntity]}, following calls to other functions of the same file
    one level deep is NOT done: only the body itself is read."""
    text = strip_c_comments(read_text("src/roomInit.c"))
    out = {}
    for m in re.finditer(r"^\w[\w\s\*]*?\b(\w+)\s*\(([^()]*)\)\s*\{", text, re.M):
        name = m.group(1)
        i = m.end()
        depth = 1
        while depth and i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        body = text[m.end():i - 1]
        refs = []
        for r in re.finditer(r"\bLoadRoomEntity(List)?\s*\(\s*(?:\([^()]*\))?\s*&?\s*(\w+)\s*(\[[^\]]*\])?\s*\)", body):
            sym = r.group(2)
            if sym in ("GetCurrentRoomProperty", "GetRoomProperty"):
                continue
            refs.append({"label": sym, "single": r.group(1) is None, "indexed": bool(r.group(3))})
        for r in re.finditer(r"\bLoadRoomEntityList\s*\(\s*(?:\([^()]*\))?\s*GetCurrentRoomProperty\s*\(\s*(\d+)\s*\)", body):
            refs.append({"property": int(r.group(1))})
        if refs:
            out[name] = refs
    return out
