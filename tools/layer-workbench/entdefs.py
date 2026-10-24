"""Enemy and projectile definition tables (src/enemy.c, src/projectile.c), read from the ROM.

Objects and NPCs have their 8-byte definition tables in sprites.py (ObjectDefinitions and its NPC twin);
enemies and projectiles use a 16-byte record (include/definitions.h):

    u16 gfx            0: none; bit 15: tiles already in VRAM (common gfx); bit 14: swapped gfx, low
                       bits >> 4 is the slot count (LoadSwapGFX); else a fixed gfx index (LoadFixedGFX)
    u16 paletteIndex   LoadObjPalette id
    u32 ptr            hitbox, or (gfx == 0xFFFF, MULTI_FORM) the table of forms picked by entity->type
    u16 spriteIndex    (projectile: low 12 bits)
    u8  spriteFlags    spritePriority:3 collision:1 draw:2 shadow:2
    u8  health         (projectile: field0x40)
    s16 speed
    u8  damageType, collisionMask

The tables are not in port/port_rom.c's RomOffsets (the port compiles them from the C source), so they are
found by content: the C source gives, per entry, the numbers that identify it (gfx, palette, flags, health,
speed, damage, mask; the hitbox pointer and the sprite index are left as wildcards), the ROM is searched for
the whole run, and the match has to be unique. The sprite indices are then compared with the source's
(enum Sprites, shifted by one past index 288 in the EU layout: Port_RemapLogicalSpriteIndexForRegion).
"""
import re
import struct

import tmcdata as T

EU_OMITTED_SPRITE = 288


# --------------------------------------------------------------------------- a small C preprocessor

def _cond(expr, defs):
    expr = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda m: "1" if defs.get(m.group(1)) else "0", expr)
    expr = expr.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    if not re.fullmatch(r"[\s01()andorn t]*", expr):
        raise ValueError("cannot evaluate #if %r" % expr)
    return bool(eval(expr, {"__builtins__": {}}, {}))


def preprocess(text, defs):
    """The lines of text that survive #if/#ifdef/#ifndef/#elif/#else/#endif with the given macros defined."""
    out = []
    stack = []      # (this branch active, some earlier branch was taken, parent active)
    active = True
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", s)
        if not m:
            if active:
                out.append(line)
            continue
        d, rest = m.group(1), m.group(2).strip()
        if d in ("if", "ifdef", "ifndef"):
            c = bool(defs.get(rest.split()[0])) if d == "ifdef" else (not defs.get(rest.split()[0])) if d == "ifndef" \
                else _cond(rest, defs)
            stack.append((c, c, active))
            active = active and c
        elif d == "elif":
            cur, taken, parent = stack.pop()
            c = (not taken) and _cond(rest, defs)
            stack.append((c, taken or c, parent))
            active = parent and c
        elif d == "else":
            cur, taken, parent = stack.pop()
            stack.append((not taken, True, parent))
            active = parent and not taken
        else:
            cur, taken, parent = stack.pop()
            active = parent
    if stack:
        raise ValueError("unbalanced #if")
    return "\n".join(out)


def split_top(s, sep=","):
    """Split at depth-0 separators; parentheses and braces nest."""
    parts, depth, cur = [], 0, []
    for ch in s:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def c_arrays(text, typename):
    """{array name: [("multi", target) | ("rec", [field tokens])]} for the arrays of one type in already
    preprocessed, comment-stripped C text."""
    out = {}
    for m in re.finditer(r"%s\s+(\w+)\s*\[\]\s*=\s*\{" % re.escape(typename), text):
        i, depth = m.end(), 1
        while depth:
            c = text[i]
            depth += (c == "{") - (c == "}")
            i += 1
        items = []
        for it in split_top(text[m.end():i - 1]):
            mm = re.fullmatch(r"MULTI_FORM\(\s*(\w+)\s*\)", it)
            if mm:
                items.append(("multi", mm.group(1)))
            elif it.startswith("{"):
                items.append(("rec", split_top(it[1:-1])))
            else:
                raise ValueError("unexpected item %r in %s" % (it[:40], m.group(1)))
        out[m.group(1)] = items
    return out


_SPRITE_ENUM = []


def sprites_enum():
    """enum Sprites of include/definitions.h: name -> USA index."""
    if not _SPRITE_ENUM:
        text = T.strip_c_comments(T.read_text("include/definitions.h"))
        m = re.search(r"typedef enum \{\s*(SPRITE_0,.*?)\}\s*Sprites;", text, re.S)
        names = {}
        n = 0
        for tok in split_top(m.group(1)):
            if "=" in tok:
                tok, v = [x.strip() for x in tok.split("=")]
                n = int(v, 0)
            names[tok] = n
            n += 1
        _SPRITE_ENUM.append(names)
    return _SPRITE_ENUM[0]


def native_sprite(token, region):
    """A sprite token of the source (SPRITE_X or a literal) -> (index in the region's layout or None,
    literal?). None for the entry the EU layout omits."""
    token = token.strip()
    lit = re.fullmatch(r"\d+|0x[0-9a-fA-F]+", token) is not None
    v = int(token, 0) if lit else sprites_enum()[token]
    if not lit and region == "EU":
        if v == EU_OMITTED_SPRITE:
            return None, lit
        if v > EU_OMITTED_SPRITE:
            v -= 1
    return v, lit


# --------------------------------------------------------------------------- the tables


class CDefinitions:
    """gEnemyDefinitions / gProjectileDefinitions of the ROM's region."""

    SPECS = {
        "ENEMY": dict(file="src/enemy.c", type="EnemyDefinition", name="gEnemyDefinitions",
                      sprite_mask=0xFFFF),
        "PROJECTILE": dict(file="src/projectile.c", type="ProjectileDefinition", name="gProjectileDefinitions",
                           sprite_mask=0x0FFF),
    }

    def __init__(self, rom, kind):
        self.rom, self.kind = rom, kind
        sp = self.SPECS[kind]
        self.spec = sp
        eu = rom.region == "EU"
        text = T.strip_c_comments(T.read_text(sp["file"]))
        text = preprocess(text, {"EU": eu, "MULTI_REGION": False})
        arrays = c_arrays(text, sp["type"])
        self.arrays = arrays
        self.items = arrays[sp["name"]]
        self.base = self.locate()

    def _tok(self, rec, i):
        return rec[i]

    def locate(self):
        pats = []
        for kind, v in self.items:
            if kind == "multi":
                pats.append(b"\xff\xff\x00\x00.{4}\x00\x00\x00\x00\x00\x00\x00\x00")
                continue
            pats.append(self.pattern(v))
        rx = re.compile(b"".join(pats), re.S)
        found = [m.start() for m in rx.finditer(self.rom.data)]
        if len(found) != 1:
            raise ValueError("%s definitions: %d matches in the ROM (want exactly 1)" % (self.kind, len(found)))
        return found[0]

    def pattern(self, t):
        """16-byte regex of a record's tokens (hitbox pointer and sprite index are wildcards)."""
        gfx, pal = int(t[0], 0), int(t[1], 0)
        if self.kind == "ENEMY":
            flags, health, speed, dmg, mask = t[4], t[5], t[6], t[7], t[8]
        else:
            flags, health, speed, dmg, mask = t[5], t[6], t[7], t[8], t[9]
        fl = [int(x, 0) for x in flags.strip("{} ").split(",")]
        fb = fl[0] | fl[1] << 3 | fl[2] << 4 | fl[3] << 6
        b = struct.pack("<HH", gfx, pal) + b"\x00\x00\x00\x00" + b"\x00\x00"
        b = bytearray(b)
        pat = re.escape(bytes(b[:4])) + b".{4}" + b".{2}" + re.escape(
            struct.pack("<BBhBB", fb, int(health, 0), int(speed, 0), int(dmg, 0), int(mask, 0)))
        return pat

    def record(self, o):
        d = self.rom.data
        gfx, pal, ptr, spr = struct.unpack_from("<HHIH", d, o)
        fl, health, speed, dmg, mask = struct.unpack_from("<BBhBB", d, o + 10)
        return {"gfx": gfx, "palette": pal, "ptr": ptr, "sprite": spr & self.spec["sprite_mask"],
                "priority": fl & 7, "collision": (fl >> 3) & 1, "draw": (fl >> 4) & 3, "shadow": (fl >> 6) & 3,
                "health": health, "speed": speed, "damage": dmg, "mask": mask, "raw": d[o:o + 16].hex()}

    def n_forms(self, entry_id):
        kind, v = self.items[entry_id]
        return len(self.arrays[v]) if kind == "multi" else 1

    def get(self, ent_id, ent_type):
        """GetEnemyDefinition / the projectile equivalent: the record for (id, type), or None when the id or
        the form does not exist."""
        if ent_id >= len(self.items):
            return None
        o = self.base + 16 * ent_id
        rec = self.record(o)
        if rec["gfx"] == 0xFFFF:
            if ent_type >= self.n_forms(ent_id):
                return None
            at = self.rom.read_ptr(o + 4)
            if at is None:
                return None
            rec = self.record(at + 16 * ent_type)
            rec["form"] = ent_type
        return rec

    # ---- checks used by test_maps.py
    def compare_with_source(self):
        """Every entry (and every form of the multi-form ones) against the C source: gfx, palette, flags, health,
        speed, damage, mask exactly; the sprite index against enum Sprites in the region's layout (literals the
        source marks 'TODO sprite index too high' are counted, not compared). Returns (compared, literals,
        problems)."""
        compared = lits = 0
        problems = []

        def one(tag, at, t):
            nonlocal compared, lits
            rec = self.record(at)
            if not re.fullmatch(self.pattern(t), self.rom.data[at:at + 16], re.S):
                problems.append("%s: bytes %s do not match the source numbers" % (tag, rec["raw"]))
                return
            sprite_tok = t[3]
            v, lit = native_sprite(sprite_tok, self.rom.region)
            if lit:
                lits += 1
            elif v is not None and v != rec["sprite"]:
                problems.append("%s: sprite %s is %d in the ROM, %d by the source" % (tag, sprite_tok, rec["sprite"], v))
                return
            compared += 1

        for i, (kind, v) in enumerate(self.items):
            at = self.base + 16 * i
            if kind == "rec":
                one("%s %d" % (self.kind, i), at, v)
                continue
            sub = self.rom.read_ptr(at + 4)
            if sub is None:
                problems.append("%s %d: MULTI_FORM pointer is not in the ROM" % (self.kind, i))
                continue
            for k, (kk, tv) in enumerate(self.arrays[v]):
                if kk != "rec":
                    problems.append("%s %d form %d is not a record" % (self.kind, i, k))
                    continue
                one("%s %d form %d (%s)" % (self.kind, i, k, v), sub + 16 * k, tv)
        return compared, lits, problems
