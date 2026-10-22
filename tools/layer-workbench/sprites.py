"""Sprites as pictures: the OBJ side of the layer workbench.

Entities are only markers in maps.json. For a curated list of families this
module works out how the game draws them at rest and renders a small RGBA
picture per entity, which build_maps.py writes to sprites.json.

How the game draws an object, and where each step is read from here:

  ObjectInit / LoadObjectSprite (src/objectUtils.c)
      gObjectDefinitions[id]  -> sprite index, OBJ palette id, gfx index and
      gfx type (0 fixed, 1 swapped per frame, 2 already in VRAM). The table is
      in the ROM; it is not in port/port_rom.c's RomOffsets (the port compiles
      it from src/objectDefinitions_data.inc), so it is found by content and
      checked against that file (see ObjectDefinitions).
  The entity's init code (src/object/*.c) -> which animation or frame, which
      flips, which offsets. That is the part that differs per kind and is the
      RULES table below; every rule cites the function it comes from.
  InitializeAnimation / FrameZero (port/port_animation.c, asm
      InitializeAnimation): gSpritePtrs[sprite].animations[anim] is a list of
      4-byte frames {frameIndex, duration, spriteSettings, flags}; the first one
      sets entity->frameIndex and frameSpriteSettings (whose flip bits are XORed
      with the entity's own in ResolveEntitySpriteParams, port/port_draw.c).
  LookupFrameData / RenderSpritePieces (port/port_draw.c): gFrameObjLists[sprite]
      -> a table of frame offsets -> {count, count * (x, y, shape|size|flip,
      tile low, tile high | palette addend)}; the OBJ size table at
      overlaySizeTable gives each piece's anchor (what a flip mirrors about) and
      pixel size.
  LoadFixedGFX / sub_080ADDD8 (src/vram.c): gFixedTypeGfxData[gfx] is an offset
      into gGlobalGfxAndPalettes (bit 0: LZ77) and a size; the tiles land at the
      entity's spriteVramOffset, so piece tile n is tile n of that blob.
  UpdateSpriteGfxSlot (port/port_animation.c), for swapped gfx: the tiles of
      the current frame, SpriteFrame{numTiles, firstTileIndex} of
      gSpritePtrs[sprite].frames, copied from gSpritePtrs[sprite].ptr.
  LoadObjPalette / LoadObjPaletteAtIndex (src/color.c): ids 0-5 are the fixed OBJ
      palette rows, 6-0x14 copy BG palette (id - 6), 0x15 fills one colour, and
      0x16.. index gUnk_08133368 (the ROM's objPalettes): {offset in
      gGlobalGfxAndPalettes, count << 24}.

Only what is certain is drawn. Anything that depends on runtime state (a flag,
the inventory, the player's position) is drawn in one stated pose and the pose is
recorded in the entry's "state" text; an entity the rules cannot resolve gets no
picture (a wrong picture is worse than the marker) and a "skipped" reason.
"""
import hashlib
import re
import struct

import tmcdata as T

SPRITES_VERSION = 2

# --------------------------------------------------------------------------- ROM tables


class ObjectDefinitions:
    """gObjectDefinitions (include/definitions.h, src/objectDefinitions_data.inc):
    8 bytes per object id.

        byte 0   bits 0-1 type (0 no sprite, 1 one form, 2/3 pointer to a form
                 per entity type), bits 2-3 flags
        byte 1   hitbox
        u16      gfx:10, gfx_type:2, unk2:4
        u16      paletteIndex:10, unk:2, shadow:2, unk2:2
        u16      spriteIndex:10, spritePriority:3, draw:3

    NPCs use the very same record (NPCDefinitions below, src/npcDefinitions_data.inc).
    """

    SOURCE = "src/objectDefinitions_data.inc"
    TABLE = "gObjectDefinitions"
    PASS_MACRO = "OBJDEF_EU_PASS"

    def __init__(self, rom):
        self.rom = rom
        self.inc, self.multi, self.forms = parse_inc_tables(self.SOURCE, self.TABLE, self.PASS_MACRO, rom.region == "EU")
        self.base = self.locate()

    def locate(self):
        # Entries 3..6 of the table are plain records in both regions (not
        # forms): search the ROM for their bytes, which the source file gives.
        # (NPCs: the first run of four plain records.)
        run = []
        first = None
        for i, e in enumerate(self.inc):
            if e is None or any(not isinstance(x, int) for x in e):
                run = []
                first = None
                continue
            if first is None:
                first = i
            run.append(e)
            if len(run) == 4:
                break
        if len(run) < 4:
            raise ValueError("%s: no run of plain entries to anchor on" % self.SOURCE)
        pat = b"".join(encode_definition(e) for e in run)
        at = self.rom.data.find(pat)
        if at < 0 or self.rom.data.find(pat, at + 1) >= 0:
            raise ValueError("%s not found (or not unique) in the ROM" % self.TABLE)
        return at - first * 8

    def record(self, o):
        d = self.rom.data
        b0, hitbox = d[o], d[o + 1]
        g, p, s = struct.unpack_from("<3H", d, o + 2)
        return {
            "type": b0 & 3, "flags": (b0 >> 2) & 3, "hitbox": hitbox,
            "gfx": g & 0x3FF, "gfxType": (g >> 10) & 3, "unk2": g >> 12,
            "palette": p & 0x3FF, "shadow": (p >> 12) & 3,
            "sprite": s & 0x3FF, "spritePriority": (s >> 10) & 7, "draw": s >> 13,
            "raw": d[o:o + 8].hex(),
        }

    def n_forms(self, obj_id):
        """How many forms entry obj_id has (1 for a plain record), from the source."""
        if obj_id >= len(self.multi) or self.multi[obj_id] is None:
            return 1
        return len(self.forms[self.multi[obj_id]])

    def get(self, obj_id, etype):
        """LoadObjectSprite: the record the game uses for (id, entity type). None when the entry
        has no forms for that type (the game would read past the form table) or no pointer."""
        o = self.base + 8 * obj_id
        rec = self.record(o)
        if rec["type"] in (2, 3):
            ptr = self.rom.read_ptr(o + 4)
            if ptr is None:
                return None
            if obj_id < len(self.multi) and self.multi[obj_id] is not None and etype >= self.n_forms(obj_id):
                return None
            rec = self.record(ptr + 8 * etype)
            rec["form"] = etype
        return rec


class NPCDefinitions(ObjectDefinitions):
    """gNPCDefinitions (NPCInit, src/npcUtils.c): the object definition's record and form rule
    (a form per entity type when type == 2); there is no 'flags' use and no gfx type 2 difference."""
    SOURCE = "src/npcDefinitions_data.inc"
    TABLE = "gNPCDefinitions"
    PASS_MACRO = "NPCDEF_EU_PASS"


_INC_ITEM = re.compile(r"MULTI_FORM\(\s*NAME\(\s*(\w+)\s*\)\s*\)|\{\s*\{([^{}]*)\}\s*,\s*\{([^{}]*)\}\s*\}")


def parse_inc_tables(rel, table, macro, eu):
    """src/*Definitions_data.inc for one region: (top, multi, forms).
    top[id]    the plain record's 14 numbers (type, flags, unk, hitbox, gfx, gfx_type, unk2, pal, unk,
               shadow, unk2, sprite, prio, draw), None for a MULTI_FORM entry (or numbers that are symbols)
    multi[id]  the form array's name for a MULTI_FORM entry, else None
    forms      {array name: [numbers or None per form]}"""
    text = T.read_text(rel)
    top, multi, forms = [], [], {}
    stack = []          # active? per #if level
    cur = None          # name of the array being read
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#ifdef " + macro):
            stack.append(eu)
            continue
        if s.startswith("#ifndef " + macro):
            stack.append(not eu)
            continue
        if s.startswith("#else"):
            stack[-1] = not stack[-1]
            continue
        if s.startswith("#endif"):
            stack.pop()
            continue
        if s.startswith("#"):
            raise ValueError("unexpected directive in %s: %r" % (rel, s))
        if stack and not all(stack):
            continue
        m = re.search(r"NAME\(\s*(\w+)\s*\)\s*\[\]\s*=\s*\{", s)
        if m:
            cur = m.group(1)
            if cur != table:
                forms[cur] = []
            continue
        if s.startswith("};"):
            cur = None
            continue
        if cur is None:
            continue
        for mm in _INC_ITEM.finditer(s):
            if mm.group(1):
                if cur != table:
                    raise ValueError("%s: MULTI_FORM inside %s" % (rel, cur))
                top.append(None)
                multi.append(mm.group(1))
                continue
            try:
                nums = [int(x) for x in (mm.group(2) + "," + mm.group(3)).split(",")]
                if len(nums) != 14:
                    raise ValueError("bad definition line %r" % s)
            except ValueError as e:
                if "bad definition" in str(e):
                    raise
                nums = None     # a symbolic sprite index (SPRITE_X): only the count matters
            if cur == table:
                top.append(nums)
                multi.append(None)
            else:
                forms[cur].append(nums)
    return top, multi, forms


def parse_inc_table(eu):
    """The object table alone (kept for the tests): list per id of the 14 numbers or None."""
    return parse_inc_tables(ObjectDefinitions.SOURCE, ObjectDefinitions.TABLE, ObjectDefinitions.PASS_MACRO, eu)[0]


def encode_definition(n):
    t, fl, unk, hb, gfx, gt, u2, pal, u_, sh, u2b, spr, pr, dr = n
    return (bytes([t | fl << 2 | unk << 4, hb])
            + struct.pack("<3H", gfx | gt << 10 | u2 << 12, pal | u_ << 10 | sh << 12 | u2b << 14,
                          spr | pr << 10 | dr << 13))


class SpriteTables:
    """Everything between a sprite index and pixels, read from the ROM."""

    def __init__(self, rom):
        self.rom = rom
        o = rom.off
        self.sprite_ptrs = o["spritePtrs"]
        self.frame_lists = o["frameObjLists"]
        cfg = port_config_defines()
        r = rom.region
        self.frame_lists_size = cfg["PORT_%s_FRAME_OBJ_LISTS_SIZE" % r]
        self.frame_lists_count = cfg["PORT_%s_FRAME_OBJ_COUNT" % r]
        self.fixed_gfx = o["fixedTypeGfx"]
        self.fixed_gfx_count = cfg["PORT_%s_FIXED_TYPE_GFX_COUNT" % r]
        self.obj_palettes = o["objPalettes"]
        self.obj_palettes_count = o["objPalettesCount"]
        self.gfx_base = o["gfxAndPalettes"]
        d = rom.data
        self.size_table = bytes(d[o["overlaySizeTable"]:o["overlaySizeTable"] + 240])
        self.defs = ObjectDefinitions(rom)
        self._blobs = {}
        # Count of valid gSpritePtrs entries: the port walks the table while the
        # animations pointer lies in the ROM (port_rom.c, "gSpritePtrs").
        n = 0
        while n < 512:
            a = rom.u32(self.sprite_ptrs + 16 * n)
            if a != 0 and not (0x08000000 <= a < 0x08000000 + len(d)):
                break
            n += 1
        self.sprite_ptr_count = n

    # ---- gSpritePtrs
    def sprite_ptr(self, idx):
        """(animations, frames, tiles) ROM offsets (None where 0) or None."""
        if idx >= self.sprite_ptr_count:
            return None
        b = self.sprite_ptrs + 16 * idx
        return tuple(self.rom.read_ptr(b + 4 * k) for k in range(3))

    def first_frame(self, sprite, anim):
        """InitializeAnimation -> FrameZero: the animation's first frame
        {frameIndex, duration, spriteSettings, flags} (4 bytes, ROM offset)."""
        sp = self.sprite_ptr(sprite)
        if sp is None or sp[0] is None:
            return None
        a = self.rom.read_ptr(sp[0] + 4 * anim)
        if a is None:
            return None
        fi, dur, ss, fl = self.rom.data[a:a + 4]
        return {"frameIndex": fi, "duration": dur, "settings": ss, "flags": fl, "at": a}

    def animation_count(self, sprite):
        sp = self.sprite_ptr(sprite)
        if sp is None or sp[0] is None:
            return 0
        n = 0
        while self.rom.read_ptr(sp[0] + 4 * n) is not None and n < 64:
            n += 1
        return n

    # ---- gFrameObjLists
    def pieces(self, sprite, frame):
        """LookupFrameData + the piece format of RenderSpritePieces. Returns a
        list of dicts, or None when the frame is not in the table."""
        if sprite >= self.frame_lists_count:
            return None
        base, size = self.frame_lists, self.frame_lists_size
        d = self.rom.data
        tbl = struct.unpack_from("<I", d, base + 4 * sprite)[0]
        if tbl > size - 4:
            return None
        ent = tbl + 4 * frame
        if ent > size - 4:
            return None
        off = struct.unpack_from("<I", d, base + ent)[0]
        if off >= size:
            return None
        count = d[base + off]
        if off + 1 + 5 * count > size:
            return None
        out = []
        for i in range(count):
            x, y, si, tl, th = struct.unpack_from("<bbBBB", d, base + off + 1 + 5 * i)
            out.append({"x": x, "y": y, "shape": si >> 6, "size": (si >> 4) & 3,
                        "hflip": (si >> 2) & 1, "vflip": (si >> 3) & 1, "clearPal": si & 1,
                        "tile": tl | (th & 3) << 8, "palAdd": th >> 4, "prioAdd": (th >> 2) & 3,
                        "raw": "%02x%02x%02x%02x%02x" % (x & 255, y & 255, si, tl, th)})
        return out

    def frame_table_extent(self, sprite):
        """Number of frames of a sprite, or None when it cannot be told. The frame tables of gFrameObjLists
        are laid out one after the other, in sprite order, ahead of all the frame data, so a table runs up to the
        next sprite's table; where the order breaks (55 sprites, mostly past 330) or for the last sprite the
        length is not stored anywhere and is unknown. A frame index at or past the end reads another sprite's
        table (the game does that for the bytes animations name, not meaning them as frames)."""
        base = self.frame_lists
        d = self.rom.data
        if sprite + 1 >= self.frame_lists_count:
            return None
        tbl = struct.unpack_from("<I", d, base + 4 * sprite)[0]
        nxt = struct.unpack_from("<I", d, base + 4 * (sprite + 1))[0]
        return (nxt - tbl) // 4 if nxt > tbl else None

    # ---- tiles
    def fixed_blob(self, gfx):
        """gFixedTypeGfxData[gfx] (sub_080ADDD8): the tiles LoadFixedGFX puts
        into the entity's slot."""
        if gfx in self._blobs:
            return self._blobs[gfx]
        if gfx == 0 or gfx >= self.fixed_gfx_count:
            return None
        word = self.rom.u32(self.fixed_gfx + 4 * gfx)
        src = self.gfx_base + (word & 0xFFFFFC)
        if word & 1:
            blob = self.rom.lz77(src)
        else:
            n = ((word & 0x7F000000) >> 24) * 0x200
            blob = bytes(self.rom.data[src:src + n])
        self._blobs[gfx] = blob
        return blob

    def swap_tiles(self, sprite, frame):
        """UpdateSpriteGfxSlot: the tiles of one frame of a swapped sprite."""
        sp = self.sprite_ptr(sprite)
        if sp is None or sp[1] is None or sp[2] is None:
            return None
        n, _u, first = struct.unpack_from("<BBH", self.rom.data, sp[1] + 4 * frame)
        if n == 0:
            return None
        return bytes(self.rom.data[sp[2] + 32 * first:sp[2] + 32 * (first + n)])

    # ---- palettes
    def obj_palette(self, pal_id, room_pal, room_loaded):
        """The colour rows an entity with this OBJ palette id sees, as a list of
        16-colour rows indexed by a piece's palette addend, or None when the
        rows cannot be known statically. room_pal/room_loaded: the room's 512
        BGR555 colours (BG 0-255, OBJ 256-511) and its loaded-rows bit mask."""
        if pal_id <= 5:
            rows = []
            for k in range(6 - pal_id):
                r = pal_id + k
                if not room_loaded & (1 << (16 + r)):
                    break
                rows.append(list(room_pal[256 + 16 * r:272 + 16 * r]))
            return rows or None
        if pal_id < 0x15:
            r = pal_id - 6
            if not room_loaded & (1 << r):
                return None
            return [list(room_pal[16 * r:16 * r + 16])]
        if pal_id == 0x15:
            return None
        if pal_id - 0x16 >= self.obj_palettes_count:
            return None
        word = self.rom.u32(self.obj_palettes + 4 * (pal_id - 0x16))
        off = word & 0xFFFFFF
        n = (word >> 24) & 0xF or 1
        src = self.gfx_base + off
        return [list(struct.unpack_from("<16H", self.rom.data, src + 32 * k)) for k in range(n)]

    def abs_palette_row(self, row, room_pal, room_loaded):
        """A piece with the 'clear palette' flag names an absolute OBJ row."""
        if row > 5 or not room_loaded & (1 << (16 + row)):
            return None
        return list(room_pal[256 + 16 * row:272 + 16 * row])


def port_config_defines():
    """The numeric #defines of port/port_config.h (table sizes per region, which
    port_rom.c's RomOffsets names by macro). include/definitions.h's Sprites enum
    is 329 long but gFrameObjLists has more entries: sprites that only have
    geometry, no animation table, take their indices past gSpritePtrs."""
    out = {}
    for m in re.finditer(r"#define\s+(PORT_\w+)\s+(\d+)u?\b", T.read_text("port/port_config.h")):
        out[m.group(1)] = int(m.group(2))
    return out


def bgr555(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


# --------------------------------------------------------------------------- rendering


def render_pieces(tables, pieces, tile_data, rows, flip_x, flip_y, dx=0, dy=0):
    """The OAM entries RenderSpritePieces would write, drawn. Returns
    (picture, problems) with picture = (w, h, ox, oy, rgba bytes) where (ox, oy)
    is the picture's top-left relative to the entity's position, or None.

    flip_x/flip_y: the entity's effective flip (spriteSettings XOR the frame's).
    Position: x = xoff (negated by a flip) minus the size-table anchor of the
    flip's sub-table (0, 0x30, 0x60 or 0x90); the piece's own flip bits are
    XORed with the entity's (attr1 bits 12-13)."""
    problems = []
    sub = (0x30 if flip_x else 0) + (0x60 if flip_y else 0)
    placed = []
    for p in pieces:
        if p["shape"] == 3:
            problems.append("invalid OBJ shape 3")
            continue
        x, y = p["x"], p["y"]
        if flip_y:
            y = -y
        if flip_x:
            x = -x
        o = sub + 4 * (p["shape"] * 4 + p["size"])
        ax, ay, w, h = tables.size_table[o:o + 4]
        placed.append((x - ax + dx, y - ay + dy, w, h, p, p["hflip"] ^ (1 if flip_x else 0),
                       p["vflip"] ^ (1 if flip_y else 0)))
    if not placed:
        return None, problems
    x0 = min(q[0] for q in placed)
    y0 = min(q[1] for q in placed)
    x1 = max(q[0] + q[2] for q in placed)
    y1 = max(q[1] + q[3] for q in placed)
    W, H = x1 - x0, y1 - y0
    buf = bytearray(W * H * 4)
    for px, py, w, h, p, hf, vf in placed:
        if p["clearPal"]:
            row = tables.abs_palette_row(p["palAdd"], rows["pal"], rows["loaded"])
        else:
            base = rows["rows"]
            row = base[p["palAdd"]] if base is not None and p["palAdd"] < len(base) else None
        if row is None:
            problems.append("piece palette row %d%s not available" % (p["palAdd"], " (absolute)" if p["clearPal"] else ""))
            return None, problems
        tw = w // 8
        need = (p["tile"] + tw * (h // 8)) * 32
        if need > len(tile_data):
            problems.append("piece tiles %d..%d past the %d tiles of the gfx" %
                            (p["tile"], p["tile"] + tw * (h // 8) - 1, len(tile_data) // 32))
            return None, problems
        for yy in range(h):
            sy = h - 1 - yy if vf else yy
            for xx in range(w):
                sx = w - 1 - xx if hf else xx
                t = p["tile"] + (sy >> 3) * tw + (sx >> 3)
                b = tile_data[t * 32 + (sy & 7) * 4 + ((sx & 7) >> 1)]
                ci = (b >> 4) if sx & 1 else (b & 15)
                if ci == 0:
                    continue
                r, g, bl = bgr555(row[ci])
                o = ((py - y0 + yy) * W + (px - x0 + xx)) * 4
                buf[o:o + 4] = bytes((r, g, bl, 255))
    return (W, H, x0, y0, bytes(buf)), problems


# --------------------------------------------------------------------------- rules
#
# A rule maps one entity to a "pose": which sprite frame the game shows once the
# entity is initialised (and settled, for objects that slide in), with flips.
# Each takes (entity, ctx) and returns a dict:
#   anim / frame   what the pose is: an animation index (its first frame is
#                  used, InitializeAnimation) or a frameIndex set directly
#   flipX, flipY   spriteSettings flips set by the init code
#   dx, dy         offset added to the entity's x, y (init code, spriteOffset)
#   palette        override of the definition's OBJ palette id
#   gfx            override of the definition's fixed gfx index
#   cite           repo function the rule comes from
#   why            list of lines for the INFO panel
#   state          the runtime condition the pose depends on, or None
# or {"skip": reason} when the entity has no sprite in that state.

def flag_of(e):
    """The u16 at entity+0x86 (RegisterRoomEntity: paramC >> 16)."""
    return (e["paramC"] >> 16) & 0xFFFF


def flags_of(e):
    """The u16 at entity+0x84 (RegisterRoomEntity: paramC & 0xFFFF)."""
    return e["paramC"] & 0xFFFF


def flag_text(e, first_unused=0xFFFF):
    """'flag 0x0042' / 'flags 0x0010 or 0x0042' for the two flags the door objects
    read (u16 at 0x84, unused when 0xFFFF; u16 at 0x86, unused when 0 = BEGIN)."""
    a, b = flags_of(e), flag_of(e)
    parts = ["0x%04X" % v for v, unused in ((a, first_unused), (b, 0)) if v != unused]
    return ("flags " if len(parts) > 1 else "flag ") + " or ".join(parts) if parts else "no flag"


DIRS = ("north", "east", "south", "west")


def rule_boss_door(e, ctx):
    # src/object/bossDoor.c BossDoor_Init: type2 = type >> 2; InitializeAnimation(
    # super, type & 0x1f); direction = type & 3. type2 0: shut and drawn at once;
    # 1: appears behind the player (action 4 -> 5 -> 6), ends shut at x, y;
    # 2: shut and drawn. All three rest in the same pose at x, y.
    anim = e["type"] & 0x1F
    t2 = e["type"] >> 2
    return {
        "anim": anim, "cite": "BossDoor_Init (src/object/bossDoor.c)",
        "why": ["animation %d = type & 0x1f (type 0x%02X), first frame; faces %s (type & 3)" %
                (anim, e["type"], DIRS[e["type"] & 3]),
                "type2 = type >> 2 = %d: %s" % (t2, "drawn from the start" if t2 != 1 else
                                                 "hidden until the player has passed, then shut here")],
        "state": "shut pose; the door is deleted when %s is set (BossDoor_Init)" % flag_text(e),
    }


def fixed_door_gfx(ctx):
    """sub_080837B0 (src/object/lockedDoor.c): the fixed gfx index of the doors
    that share the locked door's tiles, by area. gUnk_0811F690 is read from the
    ROM (located by content); the two special areas are the function's."""
    area = ctx["area"]
    if area < 0x40:
        return {0x25: 0x1E7, 0x35: 0x1C3}.get(area, 0xA)
    return ctx["tables"].locked_door_gfx_by_area(area)


def rule_locked_door(e, ctx):
    # src/object/lockedDoor.c LockedDoor_Init: type2 = (type >> 2) & 3;
    # frameIndex = type & 3 (no animation); type2 2 (small key lock):
    # frameIndex |= 4. gfx: LoadFixedGFX(sub_080837B0); palette: ChangeObjPalette
    # to 0 in area 0x68, 1 in 0x88 (sub_080837B0).
    t2 = (e["type"] >> 2) & 3
    frame = e["type"] & 3
    why = ["frameIndex = type & 3 = %d (type 0x%02X): door faces %s" % (frame, e["type"], DIRS[frame])]
    if t2 == 2:
        frame |= 4
        why.append("type2 = 2 (key lock): frameIndex |= 4 -> %d" % frame)
    elif t2 == 3:
        return {"skip": "type2 = 3: LockedDoor_Init deletes the entity"}
    area = ctx["area"]
    gfx = fixed_door_gfx(ctx)
    why.append("fixed gfx %d chosen by area 0x%02X (sub_080837B0)" % (gfx, area))
    pal = {0x68: 0, 0x88: 1}.get(area)
    if pal is not None:
        why.append("area 0x%02X: ChangeObjPalette(super, %d)" % (area, pal))
    flag = flag_of(e)
    state = {0: "closed pose, drawn while flag 0x%04X is clear" % flag,
             1: "closed pose, drawn while flag 0x%04X is set (opens when it clears)" % flag,
             2: "closed pose with the keyhole, drawn while flag 0x%04X is clear" % flag}[t2]
    if t2 == 0 and e["type"] & 0x10:
        state += "; type & 0x10: slides in from outside after the player passes, rests here"
    return {"frame": frame, "gfx": gfx, "palette": pal, "cite": "LockedDoor_Init (src/object/lockedDoor.c)",
            "why": why, "state": state}


def rule_minecart_door(e, ctx):
    # src/object/minecartDoor.c MinecartDoor_Init: gfx and palette from
    # sub_080837B0 like the locked door; if (type & 4) { unk_7e = 1; type &= 3 };
    # frameIndex = type; shut and drawn (action 1, draw = 1) unless the cart
    # check (sub_08096CEC) already opens it.
    t = e["type"] & 3 if e["type"] & 4 else e["type"]
    why = ["frameIndex = type%s = %d: faces %s" % (" & 3 (bit 2 set)" if e["type"] & 4 else "", t,
                                                    DIRS[t] if t < 4 else "?")]
    if t > 3:
        return {"skip": "type %d: no frame rule" % t}
    gfx = fixed_door_gfx(ctx)
    pal = {0x68: 0, 0x88: 1}.get(ctx["area"])
    why.append("fixed gfx %d chosen by area 0x%02X (sub_080837B0)" % (gfx, ctx["area"]))
    return {"frame": t, "gfx": gfx, "palette": pal, "cite": "MinecartDoor_Init (src/object/minecartDoor.c)",
            "why": why, "state": "shut pose; opens when the minecart check (sub_08096CEC) is true"}


def rule_metal_door(e, ctx):
    # src/object/metalDoor.c MetalDoor_Init: frameIndex = 0; spriteSettings.flipY = 1
    # (the door is one frame, flipped for the south wall); hidden until the player
    # walks past (MetalDoor_Action1), then slides 0x24 px back to x, y.
    return {"frame": 0, "flipY": 1, "cite": "MetalDoor_Init (src/object/metalDoor.c)",
            "why": ["frameIndex = 0, spriteSettings.flipY = 1 (set in MetalDoor_Init; no animation)",
                    "MetalDoor_Action1/2 draw it 0x24 px away and move it back, so it rests at x, y"],
            "state": "shut pose after the player has passed; not drawn before; deleted when %s is set"
            % flag_text(e, first_unused=-1)}


def rule_gentari_curtain(e, ctx):
    # src/object/gentariCurtain.c GentariCurtain_Init: x += 2 in both branches;
    # flag clear (first Init): InitAnimationForceUpdate(super, 0), the shut curtain;
    # flag set: action 3, InitAnimationForceUpdate(super, 1), the open one.
    return {"anim": 0, "dx": 2, "cite": "GentariCurtain_Init (src/object/gentariCurtain.c)",
            "why": ["x += 2 (GentariCurtain_Init)", "animation 0 = shut curtain (animation 1 is the open one)"],
            "state": "shut pose; with flag 0x%04X set the curtain shows animation 1 (open) instead" % flag_of(e)}


def rule_chest_spawner(e, ctx):
    # src/object/chestSpawner.c: ChestSpawner_Types[type]: 2-5 are Type2 (a chest
    # object); Type2Init: InitializeAnimation(super, 0) before the flag, animation
    # 1 once local flag type2 is set (opened). Types 0, 1, 6, 7 are Type0: they
    # only SetTile (sub_0807B7D8, tile 0x73) when the flag is set, no sprite.
    if e["type"] in (0, 1, 6, 7):
        return {"skip": "type %d is ChestSpawner_Type0: a BG tile set at runtime, no sprite" % e["type"]}
    if e["type"] > 7:
        return {"skip": "type %d is out of ChestSpawner_Types" % e["type"]}
    return {"anim": 0, "cite": "ChestSpawner_Type2Init (src/object/chestSpawner.c)",
            "why": ["type %d is ChestSpawner_Type2; InitializeAnimation(super, 0) = the shut chest" % e["type"]],
            "state": "shut chest; it appears when flag 0x%04X is set (type 4: at once); with local flag %d set "
                     "the game shows animation 1 (opened)" % (flag_of(e), e["type2"])}


def rule_light_door(e, ctx):
    # src/object/lightDoor.c LightDoor_Init: type 0: spritePriority 7, frameIndex = 0
    # (deleted when the flag is set); other types are the light-beam effect.
    if e["type"] != 0:
        return {"skip": "type %d is the light effect, not the door" % e["type"]}
    return {"frame": 0, "cite": "LightDoor_Init (src/object/lightDoor.c)",
            "why": ["type 0: frameIndex = 0"], "state": "drawn until flag 0x%04X is set" % flag_of(e)}


def rule_trapdoor(e, ctx):
    # src/object/trapdoor.c Trapdoor_Init: with the flippers: action 4, frameIndex 0
    # (open); else InitializeAnimation(this, 0), the closed trapdoor.
    return {"anim": 0, "cite": "Trapdoor_Init (src/object/trapdoor.c)",
            "why": ["without the Flippers: InitializeAnimation(this, 0)"],
            "state": "pose without the Flippers in the inventory; with them the game shows frameIndex 0 directly"}


RULES = {
    ("OBJECT", "BOSS_DOOR"): rule_boss_door,
    ("OBJECT", "LOCKED_DOOR"): rule_locked_door,
    ("OBJECT", "METAL_DOOR"): rule_metal_door,
    ("OBJECT", "MINECART_DOOR"): rule_minecart_door,
    ("OBJECT", "GENTARI_CURTAIN"): rule_gentari_curtain,
    ("OBJECT", "CHEST_SPAWNER"): rule_chest_spawner,
    ("OBJECT", "LIGHT_DOOR"): rule_light_door,
    ("OBJECT", "TRAPDOOR"): rule_trapdoor,
}

# What each rule relies on in the sources: (file, regex that must still match). test_maps.py runs them, so a
# change in the decomp's init code that would make a rule stale is noticed.
RULE_SOURCES = {
    ("OBJECT", "BOSS_DOOR"): [("src/object/bossDoor.c", r"InitializeAnimation\(super, super->type & 0x1f\)"),
                              ("src/object/bossDoor.c", r"super->type2 = super->type >> 2")],
    ("OBJECT", "LOCKED_DOOR"): [("src/object/lockedDoor.c", r"super->frameIndex = super->type & 3"),
                                ("src/object/lockedDoor.c", r"super->frameIndex \|= 4"),
                                ("src/object/lockedDoor.c", r"super->type2 = \(super->type >> 2\) & 3"),
                                ("src/object/lockedDoor.c", r"ChangeObjPalette\(super, 0\)"),
                                ("src/object/lockedDoor.c", r"ChangeObjPalette\(super, 1\)"),
                                ("src/object/lockedDoor.c", r"case 0x25:\s*tmp = 0x1e7;"),
                                ("src/object/lockedDoor.c", r"case 0x35:\s*tmp = 0x1c3;")],
    ("OBJECT", "MINECART_DOOR"): [("src/object/minecartDoor.c", r"super->frameIndex = super->type;"),
                                  ("src/object/minecartDoor.c", r"super->type &= 3;"),
                                  ("src/object/minecartDoor.c", r"sub_080837B0\(super\)")],
    ("OBJECT", "METAL_DOOR"): [("src/object/metalDoor.c", r"super->frameIndex = 0;"),
                               ("src/object/metalDoor.c", r"super->spriteSettings.flipY = 1;")],
    ("OBJECT", "GENTARI_CURTAIN"): [("src/object/gentariCurtain.c", r"InitAnimationForceUpdate\(super, 0\)"),
                                    ("src/object/gentariCurtain.c", r"InitAnimationForceUpdate\(super, 1\)"),
                                    ("src/object/gentariCurtain.c", r"super->x.HALF.HI \+= 2")],
    ("OBJECT", "CHEST_SPAWNER"): [("src/object/chestSpawner.c", r"InitializeAnimation\(super, 0\)"),
                                  ("src/object/chestSpawner.c", r"InitializeAnimation\(super, 1\)"),
                                  ("src/object/chestSpawner.c", r"ChestSpawner_Type0, ChestSpawner_Type0, ChestSpawner_Type2, "
                                   r"ChestSpawner_Type2,\s*ChestSpawner_Type2, ChestSpawner_Type2, ChestSpawner_Type0, "
                                   r"ChestSpawner_Type0")],
    ("OBJECT", "LIGHT_DOOR"): [("src/object/lightDoor.c", r"super->frameIndex = 0;")],
    ("OBJECT", "TRAPDOOR"): [("src/object/trapdoor.c", r"InitializeAnimation\(this, 0\)")],
}

# Families looked at and left as markers, with the reason (shown by the test and README).
NOT_COVERED = {
    ("OBJECT", "SPECIAL_CHEST"): "ObjectDefinition has no sprite (it only marks a chest tile)",
    ("OBJECT", "HOUSE_DOOR_EXT"): "a spawner of doors from the room's property table; the doors are created at runtime",
    ("OBJECT", "HOUSE_DOOR_INT"): "frame depends on the form read at runtime from the entity's type2/properties",
    ("OBJECT", "ROTATING_TRAPDOOR"): "tile-type dependent and transient (deletes itself after one animation)",
}


def locked_door_gfx_table(rom):
    """gUnk_0811F690 (src/object/lockedDoor.c): fixed gfx index per area >= 0x40.
    Parsed from the source, located in the ROM by content and compared."""
    text = T.strip_c_comments(T.read_text("src/object/lockedDoor.c"))
    m = re.search(r"gUnk_0811F690\[\]\s*=\s*\{(.*?)\};", text, re.S)
    vals = [int(x, 0) for x in m.group(1).replace("\n", " ").split(",") if x.strip()]
    pat = struct.pack("<%dH" % len(vals), *vals)
    at = rom.data.find(pat)
    if at < 0:
        raise ValueError("gUnk_0811F690 not found in the ROM")
    rom_vals = list(struct.unpack_from("<%dH" % len(vals), rom.data, at))
    return rom_vals, at


# --------------------------------------------------------------------------- build


DEFAULT_KINDS = ("OBJECT", "ENEMY", "NPC", "PROJECTILE")


def b64(data):
    import base64
    return base64.b64encode(data).decode("ascii")


class SpriteBuilder:
    def __init__(self, rom, enums):
        self.rom = rom
        self.enums = enums
        self.tables = SpriteTables(rom)
        gfx_vals, self.door_table_at = locked_door_gfx_table(rom)
        self.door_gfx = gfx_vals
        self.tables.locked_door_gfx_by_area = lambda a: self.door_gfx[a - 0x40] if a - 0x40 < len(self.door_gfx) else 0xA
        self.npc_defs = NPCDefinitions(rom)
        import entdefs
        self.c_defs = {k: entdefs.CDefinitions(rom, k) for k in ("ENEMY", "PROJECTILE")}
        self.pictures = {}          # key -> picture dict
        self.rooms = {}             # "AA-RR" -> {entity index: record}
        self.sources = {}           # key -> what the viewer needs to draw any frame of a sprite as this entity
        self.sprite_cat = {}        # sprite index -> frames, animations (and the swapped tile pool)
        self.tilesets = {}          # "f<gfx>" -> base64 of a fixed gfx blob
        self.swap_sprites = set()
        self.pools = {}             # sprite index -> base64 of its swapped tile pool (sprites_tiles.json.gz, loaded lazily)
        self.stats = {"drawn": 0, "skipped": 0, "byFamily": {}, "defaultDrawn": 0, "defaultSkipped": 0,
                      "defaultByKind": {}}
        self.skips = {}             # reason -> count

    def pic_key(self, pic):
        w, h, ox, oy, rgba = pic
        k = hashlib.sha1(rgba + struct.pack("<4h", w, h, ox, oy)).hexdigest()[:12]
        if k not in self.pictures:
            self.pictures[k] = {"w": w, "h": h, "ox": ox, "oy": oy, "rgba": b64(rgba)}
        return k

    # ---- the catalog the picker draws alternatives from
    def catalog(self, sprite):
        """The animations of one sprite and the frames they use: the same tables the renderer reads, as small
        arrays. frames is sparse, {frame index: OAM pieces}: the frame tables of gFrameObjLists have no stored
        length (a frame index is a byte, 0xFF means 'draw nothing' and some animations name frames far past their
        neighbour's table), so only the frames an animation names, the ones a record draws, and, for a sprite
        without animations, the first few raw frames are listed."""
        c = self.sprite_cat.get(sprite)
        if c is not None:
            return c
        t = self.tables
        anims, refs = [], set()
        sp = t.sprite_ptr(sprite)
        if sp is not None and sp[0] is not None:
            d = self.rom.data
            for a in range(t.animation_count(sprite)):
                at = self.rom.read_ptr(sp[0] + 4 * a)
                fl, loop = [], 0
                for _ in range(64):
                    if at + 4 > len(d):
                        break
                    fi, dur, ss, fg = d[at:at + 4]
                    fl.append([fi, dur, ss, fg])
                    refs.add(fi)
                    at += 4
                    if fg & 0x80:
                        loop = max(0, len(fl) - d[at]) if at < len(d) else 0
                        break
                anims.append({"f": fl, "l": loop})
        elif sprite < t.frame_lists_count:
            refs.update(range(min(t.frame_table_extent(sprite) or 16, 16)))
        c = {"frames": {}, "anims": anims}
        self.sprite_cat[sprite] = c
        for f in sorted(refs):
            self.add_frame(sprite, f)
        return c

    def add_frame(self, sprite, f):
        c = self.sprite_cat[sprite]
        ext = self.tables.frame_table_extent(sprite)
        if f in c["frames"] or f == 0xFF or (ext is not None and f >= ext):
            return
        pcs = self.tables.pieces(sprite, f) if sprite < self.tables.frame_lists_count else None
        c["frames"][f] = [[p["x"], p["y"], p["shape"], p["size"], p["hflip"] | p["vflip"] << 1 | p["clearPal"] << 2,
                           p["tile"], p["palAdd"]] for p in (pcs or [])]

    def add_source(self, sprite, frame, mode, gfx, rows, gs):
        """Register what is needed to draw any frame of `sprite` the way an entity with these tiles and
        palette would. mode 0: fixed gfx blob; 1: swapped, tiles per frame from the sprite's own pool."""
        t = self.tables
        self.catalog(sprite)
        self.add_frame(sprite, frame)
        src = {"s": sprite, "m": mode, "r": rows,
               "a": [t.abs_palette_row(r, gs["palettes"], gs["mask"]) for r in range(6)]}
        if mode == 0:
            self.tilesets["f%d" % gfx] = b64(t.fixed_blob(gfx))
            src["t"] = "f%d" % gfx
        else:
            self.swap_sprites.add(sprite)
        key = hashlib.sha1(repr(sorted(src.items())).encode()).hexdigest()[:10]
        self.sources.setdefault(key, src)
        return key

    def swap_pool(self, sprite):
        """The tiles a swapped sprite loads, per listed frame (UpdateSpriteGfxSlot): {frame: [n, first]} and the
        pool bytes those cover."""
        t = self.tables
        sp = t.sprite_ptr(sprite)
        tab, end = {}, 0
        for f in sorted(self.sprite_cat[sprite]["frames"]):
            n, _u, first = struct.unpack_from("<BBH", self.rom.data, sp[1] + 4 * f)
            tab[f] = [n, first]
            end = max(end, first + n)
        end = min(end, (len(self.rom.data) - sp[2]) // 32)
        return tab, b64(bytes(self.rom.data[sp[2]:sp[2] + 32 * end]))

    # ---- one entity
    def entity(self, area, room_rec, gfx_set, idx, e):
        fam = (e["kindName"], e["idName"])
        if not e.get("positioned"):
            return None
        rule = RULES.get(fam)
        if rule is None:
            if e["kindName"] not in DEFAULT_KINDS or fam in NOT_COVERED:
                return None
            return self.default_entity(area, room_rec, gfx_set, e)
        name = "%s/%s" % fam
        rec = {"family": name}
        if not (0 <= e["x"] <= room_rec["width"] and 0 <= e["y"] <= room_rec["height"]):
            return self.skip(rec, name, "position %d,%d is outside the room (data quirk): marker only" % (e["x"], e["y"]))
        ctx = {"area": area, "tables": self.tables}
        pose = rule(e, ctx)
        if "skip" in pose:
            return self.skip(rec, name, pose["skip"])
        t = self.tables
        d = t.defs.get(e["id"], e["type"])
        if d is None:
            return self.skip(rec, name, "no object definition")
        sprite = d["sprite"]
        rec.update({"rule": pose["cite"], "why": list(pose["why"]), "state": pose.get("state"),
                    "sprite": sprite, "definition": d["raw"]})
        # frame and flips
        flip_x = pose.get("flipX", 0)
        flip_y = pose.get("flipY", 0)
        rec["baseFlipX"], rec["baseFlipY"] = flip_x, flip_y
        if "anim" in pose:
            ff = t.first_frame(sprite, pose["anim"])
            if ff is None:
                return self.skip(rec, name, "sprite %d has no animation %d" % (sprite, pose["anim"]))
            frame = ff["frameIndex"]
            flip_x ^= (ff["settings"] >> 6) & 1
            flip_y ^= (ff["settings"] >> 7) & 1
            rec["anim"] = pose["anim"]
            rec["why"].append("animation %d first frame: frameIndex %d, duration %d, spriteSettings 0x%02X, flags 0x%02X "
                              "(flip bits XOR the entity's)" % (pose["anim"], frame, ff["duration"], ff["settings"], ff["flags"]))
        else:
            frame = pose["frame"]
        rec["frame"] = frame
        rec["flipX"] = flip_x
        rec["flipY"] = flip_y
        pieces = t.pieces(sprite, frame)
        if not pieces:
            return self.skip(rec, name, "sprite %d frame %d has no pieces in gFrameObjLists" % (sprite, frame))
        # tiles
        gfx_type = d["gfxType"]
        gfx = pose.get("gfx", d["gfx"])
        if gfx_type == 0:
            tiles = t.fixed_blob(gfx)
            rec["gfx"] = "fixed %d" % gfx
        elif gfx_type == 1:
            tiles = t.swap_tiles(sprite, frame)
            rec["gfx"] = "swapped, frame %d of sprite %d" % (frame, sprite)
        else:
            return self.skip(rec, name, "gfx type 2: tiles are already in VRAM (not modelled)")
        if tiles is None:
            return self.skip(rec, name, "gfx %s not available" % rec["gfx"])
        pal_id = pose["palette"] if pose.get("palette") is not None else d["palette"]
        rec["palette"] = pal_id
        rows = {"rows": t.obj_palette(pal_id, gfx_set["palettes"], gfx_set["mask"]),
                "pal": gfx_set["palettes"], "loaded": gfx_set["mask"]}
        if rows["rows"] is None:
            return self.skip(rec, name, "OBJ palette %d not known statically in this room" % pal_id)
        pic, problems = render_pieces(t, pieces, tiles, rows, flip_x, flip_y, pose.get("dx", 0), pose.get("dy", 0))
        if pic is None:
            return self.skip(rec, name, "; ".join(problems) or "no pieces")
        rec["pic"] = self.pic_key(pic)
        rec["pieces"] = len(pieces)
        rec["dx"] = pose.get("dx", 0)
        rec["dy"] = pose.get("dy", 0)
        rec["src"] = self.add_source(sprite, frame, gfx_type, gfx, rows["rows"], gfx_set)
        self.stats["drawn"] += 1
        self.stats["byFamily"][name] = self.stats["byFamily"].get(name, 0) + 1
        return rec

    # ---- the default sprite of a kind with a definition table
    def definition_of(self, e):
        """(source text, record or None, gfx mode, gfx, palette, sprite, why lines) for an entity of a kind with a
        definition table; mode None when the tiles cannot be modelled (reason in the text slot)."""
        kind, i, ty = e["kindName"], e["id"], e["type"]
        if kind in ("OBJECT", "NPC"):
            tbl = self.tables.defs if kind == "OBJECT" else self.npc_defs
            src = ("gObjectDefinitions (src/objectDefinitions_data.inc, LoadObjectSprite)" if kind == "OBJECT"
                   else "gNPCDefinitions (src/npcDefinitions_data.inc, NPCInit)")
            if i >= len(tbl.inc):
                return src, None, "id %d is past the table" % i
            d = tbl.get(i, ty)
            if d is None:
                return src, None, "type %d is past the %d form(s) of this id" % (ty, tbl.n_forms(i))
            if d["type"] == 0:
                return src, None, "the definition has no sprite (type 0): the entity is drawn by its own code, if at all"
            if d["gfxType"] == 2:
                return src, d, "gfx type 2: tiles already in VRAM, loaded by something else (not modelled)"
            return src, d, None
        tbl = self.c_defs[kind]
        src = ("gEnemyDefinitions (src/enemy.c, EnemyInit/LoadEnemySprite)" if kind == "ENEMY"
               else "gProjectileDefinitions (src/projectile.c, ProjectileInit/LoadProjectileSprite)")
        if i >= len(tbl.items):
            return src, None, "id %d is past the table" % i
        d = tbl.get(i, ty)
        if d is None:
            return src, None, "type %d is past the %d form(s) of this id" % (ty, tbl.n_forms(i))
        g = d["gfx"]
        if g == 0:
            return src, d, "gfx 0: the definition loads no tiles (the entity's code does)"
        if g & 0x8000:
            return src, d, "gfx 0x%04X: common gfx already in VRAM (not modelled)" % g
        d = dict(d)
        d["gfxType"] = 1 if g & 0x4000 else 0
        d["gfx"] = g
        return src, d, None

    def default_entity(self, area, room_rec, gfx_set, e):
        kind = e["kindName"]
        name = "%s/%s" % (kind, e["idName"])
        rec = {"family": name, "default": True}
        if not (0 <= e["x"] <= room_rec["width"] and 0 <= e["y"] <= room_rec["height"]):
            return self.skip(rec, name, "position %d,%d is outside the room (data quirk): marker only" % (e["x"], e["y"]), True)
        src, d, bad = self.definition_of(e)
        rec["rule"] = "default (not curated): " + src
        if bad:
            return self.skip(rec, name, bad, True)
        t = self.tables
        sprite = d["sprite"]
        rec.update({"sprite": sprite, "definition": d["raw"]})
        why = ["default (not curated): %s gives sprite %d, OBJ palette id %d, %s" %
               (src.split(" (")[0], sprite, d["palette"],
                "fixed gfx %d" % d["gfx"] if d["gfxType"] == 0 else "swapped gfx")]
        if d.get("form") is not None:
            why.append("a MULTI_FORM entry: form %d picked by the entity's type" % d["form"])
        if sprite >= t.frame_lists_count:
            return self.skip(rec, name, "sprite index %d is past gFrameObjLists (%d)" % (sprite, t.frame_lists_count), True)
        ff = t.first_frame(sprite, 0)
        rec["baseFlipX"] = rec["baseFlipY"] = 0
        flip_x = flip_y = 0
        if ff is not None:
            frame = ff["frameIndex"]
            flip_x, flip_y = (ff["settings"] >> 6) & 1, (ff["settings"] >> 7) & 1
            rec["anim"] = 0
            why.append("animation 0 first frame: frameIndex %d, duration %d, spriteSettings 0x%02X, flags 0x%02X"
                       % (frame, ff["duration"], ff["settings"], ff["flags"]))
            state = ("animation 0, first frame, no entity flips. The init code of this kind may pick another animation "
                     "(direction, form, state), flip or palette: use the sprite picker for the alternatives")
        else:
            if d["gfxType"] == 1:
                return self.skip(rec, name, "swapped gfx but sprite %d has no animation table" % sprite, True)
            frame = 0
            why.append("sprite %d has no animation table: frame 0" % sprite)
            state = "frame 0 (the sprite has no animations); the entity's own code decides what it shows"
        if d.get("draw") == 0:
            state += "; its definition starts it undrawn (draw 0), the init code shows it later"
        rec["frame"] = frame
        rec["flipX"], rec["flipY"] = flip_x, flip_y
        if frame == 0xFF:
            return self.skip(rec, name, "animation 0 starts on frame 255, which the game does not draw", True)
        ext = t.frame_table_extent(sprite)
        if ext is not None and frame >= ext:
            return self.skip(rec, name, "animation 0 starts on frame %d, past the %d frames of sprite %d (not a plain frame)"
                             % (frame, ext, sprite), True)
        pieces = t.pieces(sprite, frame)
        if not pieces:
            return self.skip(rec, name, "sprite %d frame %d has no pieces in gFrameObjLists" % (sprite, frame), True)
        gfx_type, gfx = d["gfxType"], d["gfx"]
        if gfx_type == 0:
            tiles = t.fixed_blob(gfx)
            rec["gfx"] = "fixed %d" % gfx
        else:
            tiles = t.swap_tiles(sprite, frame)
            rec["gfx"] = "swapped, frame %d of sprite %d" % (frame, sprite)
        if tiles is None:
            return self.skip(rec, name, "gfx %s not available" % rec["gfx"], True)
        pal_id = d["palette"]
        rec["palette"] = pal_id
        rows = {"rows": t.obj_palette(pal_id, gfx_set["palettes"], gfx_set["mask"]),
                "pal": gfx_set["palettes"], "loaded": gfx_set["mask"]}
        if rows["rows"] is None:
            return self.skip(rec, name, "OBJ palette %d not known statically in this room" % pal_id, True)
        pic, problems = render_pieces(t, pieces, tiles, rows, flip_x, flip_y, 0, 0)
        if pic is None:
            return self.skip(rec, name, "; ".join(problems) or "no pieces", True)
        rec["why"] = why
        rec["state"] = state
        rec["pic"] = self.pic_key(pic)
        rec["pieces"] = len(pieces)
        rec["dx"] = rec["dy"] = 0
        rec["src"] = self.add_source(sprite, frame, gfx_type, gfx, rows["rows"], gfx_set)
        self.stats["defaultDrawn"] += 1
        self.stats["defaultByKind"][kind] = self.stats["defaultByKind"].get(kind, 0) + 1
        return rec

    def skip(self, rec, name, reason, default=False):
        rec["skipped"] = reason
        if default:
            self.stats["defaultSkipped"] += 1
            key = "%s (default): %s" % (name.split("/")[0], re.sub(r"\d+", "N", reason))
        else:
            self.stats["skipped"] += 1
            key = "%s: %s" % (name, reason)
        self.skips[key] = self.skips.get(key, 0) + 1
        return rec

    def build(self, areas, gfx_sets):
        """areas: maps.json areas; gfx_sets: key -> {"palettes": [512 u16], "mask": int}."""
        for a in areas:
            for r in a["rooms"]:
                out = {}
                gs = gfx_sets[r["gfx"]]
                for i, e in enumerate(r["entities"]):
                    rec = self.entity(a["id"], r, gs, i, e)
                    if rec is not None:
                        out[str(i)] = rec
                if out:
                    self.rooms["%02X-%02X" % (a["id"], r["id"])] = out

    def result(self):
        sprites = {}
        for sprite, c in sorted(self.sprite_cat.items()):
            ent = {"frames": {str(k): v for k, v in sorted(c["frames"].items())}, "anims": c["anims"]}
            if sprite in self.swap_sprites:
                swap, pool = self.swap_pool(sprite)
                ent["swap"] = {str(k): v for k, v in swap.items()}
                self.pools[str(sprite)] = pool
            sprites[str(sprite)] = ent
        return {
            "format": "tmc-layer-workbench-sprites", "version": SPRITES_VERSION,
            "rom": {"region": self.rom.region, "sha1": self.rom.sha1},
            "pictures": self.pictures,
            "rooms": self.rooms,   # "AA-RR" (hex, as the viewer's room keys) -> {entity index in the room: record}
            "sizeTable": list(self.tables.size_table),
            "sprites": sprites,    # sprite index -> frames (OAM pieces), animations, swapped tile pool
            "sources": self.sources,   # source key -> {s sprite, m mode, t tileset, r palette rows, a absolute rows}
            "tilesets": self.tilesets,
            "notCovered": {"%s/%s" % k: v for k, v in NOT_COVERED.items()},
            "stats": self.stats,
            "skips": self.skips,
        }

    def tiles_result(self):
        """The swapped tile pools: 20 MB of base64 that only the sprite picker needs, so they live in their own
        file (gzipped) that the viewer loads on first use. Call after result()."""
        return {"format": "tmc-layer-workbench-sprite-tiles", "version": SPRITES_VERSION,
                "rom": {"region": self.rom.region, "sha1": self.rom.sha1}, "pools": self.pools}
