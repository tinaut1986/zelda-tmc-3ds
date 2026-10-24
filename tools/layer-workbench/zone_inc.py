"""Reader and writer of platform/3ds/source/port_zone_depth.inc, the port's list of stereo "zone depth fixes".

    PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)

area, room: the room the fix belongs to (numbers, hex ok); bg: 1, 2 or 3, the layer whose pixels inside the rectangle
are moved; x, y, w, h: the rectangle in ROOM pixel coordinates (the coordinates the layer workbench uses); toBg: 1, 2
or 3, the layer whose stereo plane the rectangle takes. "Draw this rectangle of layer bg at the depth of layer toBg."

Same design as depth_inc.py: a minimal-diff editor, not a serialiser. The file is hand-curated and shared, so every
line the writer does not manage (the header block, comments, blank lines, entries it cannot read) is kept byte for byte,
and only PORT_ZONE_DEPTH lines are added, changed or removed on request.

  - a line it parses is an entry; anything else is left alone, including entries inside a block comment and entries
    with a symbolic or malformed number (they are reported as `unmanaged`);
  - an entry is identified by its key (area, room, bg, x, y, w, h); its value is toBg. Editing a rectangle is a remove
    of the old key plus a set of the new one;
  - the comment lines directly above an entry (`//` or a one-line `/* */`, no blank line between, not the file's first
    line) belong to it: removing the entry removes them, changing it keeps them. A multi-line block comment is the
    header and is never attached;
  - a changed entry keeps its indentation, its spelling of the numbers and its trailing comment (only toBg changes);
  - a new entry goes after the last entry of the same room, else after the last entry, else at the end of the file;
  - the line ending of the file (LF or CRLF) and its final newline are kept.

Operations are a three-way edit (what to set, what to remove), applied to whatever the file holds now, so a change made
to the file meanwhile survives. A missing file is created with HEADER.
"""
import json
import re

import depth_inc

HEADER = """/* Hand-curated stereo 3D "zone depth fixes".
 *
 *   PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)
 *
 *   area, room  the room the fix belongs to (numbers, hex ok: 0x02, 0x00)
 *   bg          1, 2 or 3: the layer whose pixels inside the rectangle are moved
 *   x, y, w, h  the rectangle, in ROOM pixel coordinates (the coordinates the layer workbench shows for the room)
 *   toBg        1, 2 or 3: the layer whose stereo plane the rectangle takes
 *
 * Meaning: draw this rectangle of layer bg at the depth of layer toBg.
 * Example (Hyrule Town, a one-metatile hole in the log hut's BG1 roof through which BG2 shows a chick):
 *
 *   PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)
 *
 * Edited by tools/layer-workbench (the ZONES window); one entry per line, comments directly above an entry belong to it.
 */
"""

FIELDS = ("area", "room", "bg", "x", "y", "w", "h")
_NUM = r"(?:0[xX][0-9a-fA-F]+|\d+)"
_ENTRY = re.compile(r"^(?P<ind>\s*)PORT_ZONE_DEPTH\(\s*(?P<a>\w+)\s*,\s*(?P<r>\w+)\s*,\s*(?P<bg>\w+)\s*,\s*(?P<x>\w+)\s*,"
                    r"\s*(?P<y>\w+)\s*,\s*(?P<w>\w+)\s*,\s*(?P<h>\w+)\s*,\s*(?P<to>\w+)\s*\)"
                    r"(?P<tail>\s*(?://.*|/\*.*?\*/)?)\s*$")
_TO_SUB = re.compile(r"(?P<pre>^.*?PORT_ZONE_DEPTH\((?:[^,]*,){7}\s*)(?P<to>\w+)(?P<post>\s*\).*$)")


class ZoneError(ValueError):
    pass


def key_of(e):
    """(area, room, bg, x, y, w, h): what identifies an entry."""
    return tuple(e[f] for f in FIELDS)


def _num(s):
    if not re.fullmatch(_NUM, s) or (len(s) > 1 and s[0] == "0" and s[1] not in "xX"):   # C: a leading 0 means octal
        return None
    return int(s, 0)


def _classify(lines):
    """Per line: 'entry' (candidate), 'comment' (one-line comment), 'block' (in or closing a multi-line comment), 'other'."""
    kinds, in_block = [], False
    for ln in lines:
        s = ln.strip()
        if in_block:
            kinds.append("block")
            if "*/" in s:
                in_block = False
            continue
        if s.startswith("//") or (s.startswith("/*") and s.endswith("*/") and s.count("/*") == 1):
            kinds.append("comment")
        elif s.startswith("/*"):
            kinds.append("block")
            in_block = "*/" not in s
        elif "PORT_ZONE_DEPTH(" in s:
            kinds.append("entry")
        else:
            kinds.append("other")
    return kinds


class Doc:
    def __init__(self, text):
        self.eol = "\r\n" if "\r\n" in text else "\n"
        self.final_newline = text.endswith("\n")
        self.lines = text.replace("\r\n", "\n").split("\n") if text else []
        if self.final_newline:
            self.lines.pop()
        self.entries, self.unmanaged = [], []
        kinds = _classify(self.lines)
        for i, ln in enumerate(self.lines):
            if kinds[i] != "entry":
                continue
            m = _ENTRY.match(ln)
            vals = [_num(m.group(g)) for g in ("a", "r", "bg", "x", "y", "w", "h", "to")] if m else None
            if not vals or None in vals:
                self.unmanaged.append({"line": i + 1, "text": ln.strip()})
                continue
            j = i
            while j > 1 and kinds[j - 1] == "comment":      # j > 1: never the file's first line
                j -= 1
            e = dict(zip(FIELDS + ("toBg",), vals))
            e.update({"line": i + 1, "tail": m.group("tail").strip(),
                      "comment": [x.strip() for x in self.lines[j:i]], "_first": j})
            self.entries.append(e)

    def find(self, key):
        return [e for e in self.entries if key_of(e) == key]

    def public(self):
        return [{k: v for k, v in e.items() if not k.startswith("_")} for e in self.entries]

    def text(self):
        return self.eol.join(self.lines) + (self.eol if self.final_newline and self.lines else "")

    def apply(self, set_=(), remove=()):
        """Returns {"added","changed","removed","commentLinesRemoved"}. Mutates the document (entries re-read after)."""
        rep = {"added": 0, "changed": 0, "removed": 0, "commentLinesRemoved": 0}
        for k in remove:
            for e in sorted(self.find(tuple(k)), key=lambda x: -x["line"]):
                first, last = e["_first"], e["line"] - 1
                del self.lines[first:last + 1]
                rep["removed"] += 1
                rep["commentLinesRemoved"] += last - first
                self._reparse()
        for s in set_:
            k = key_of(s)
            found = self.find(k)
            if found:
                for e in found:
                    if e["toBg"] == s["toBg"]:
                        continue
                    i = e["line"] - 1
                    self.lines[i] = _TO_SUB.sub(lambda m: m.group("pre") + str(s["toBg"]) + m.group("post"), self.lines[i])
                    rep["changed"] += 1
                self._reparse()
                continue
            self.lines.insert(self._insert_at(s), self._render(s))
            rep["added"] += 1
            self._reparse()
        if any(rep.values()) and self.lines and not self.final_newline:
            self.final_newline = True
        return rep

    @staticmethod
    def _render(e):
        return "PORT_ZONE_DEPTH(0x%02X, 0x%02X, %d, %d, %d, %d, %d, %d)" % (e["area"], e["room"], e["bg"], e["x"], e["y"], e["w"], e["h"], e["toBg"])

    def _insert_at(self, s):
        same = [e for e in self.entries if (e["area"], e["room"]) == (s["area"], s["room"])]
        if same:
            return same[-1]["line"]
        if self.entries:
            return self.entries[-1]["line"]
        return len(self.lines)

    def _reparse(self):
        d = Doc(self.text())
        self.entries, self.unmanaged = d.entries, d.unmanaged


def parse(text):
    return Doc(text)


def _int(v, lo, hi, what):
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        raise ZoneError("bad %s %r (%d..%d)" % (what, v, lo, hi))
    return v


def validate_key(op):
    """Shape of a zone key from the client: area/room 0..255, bg 1..3, x/y 0..65535, w/h 1..65535."""
    if not isinstance(op, dict):
        raise ZoneError("zone is not an object")
    _int(op.get("area"), 0, 255, "area")
    _int(op.get("room"), 0, 255, "room")
    _int(op.get("bg"), 1, 3, "bg")
    _int(op.get("x"), 0, 65535, "x")
    _int(op.get("y"), 0, 65535, "y")
    _int(op.get("w"), 1, 65535, "w")
    _int(op.get("h"), 1, 65535, "h")
    return op


def check_zone(z, rooms):
    """Semantic check of one zone against the ROM's rooms: rooms maps (area, room) -> {"width", "height", "layers": set of
    1..3 the room defines}. Returns (errors, warnings), both lists of text. Errors refuse a save; warnings do not."""
    errs, warns = [], []
    r = rooms.get((z["area"], z["room"])) if rooms is not None else None
    if rooms is not None and r is None:
        return ["room %02X-%02X does not exist" % (z["area"], z["room"])], warns
    if z["bg"] == z["toBg"]:
        errs.append("bg and toBg are both %d: the zone would change nothing" % z["bg"])
    if r is not None:
        if z["x"] + z["w"] > r["width"] or z["y"] + z["h"] > r["height"]:
            errs.append("rectangle %d,%d %dx%d is not inside the room (%dx%d)" % (z["x"], z["y"], z["w"], z["h"], r["width"], r["height"]))
        if z["bg"] not in r["layers"]:
            warns.append("BG%d is not defined in room %02X-%02X" % (z["bg"], z["area"], z["room"]))
        if z["toBg"] not in r["layers"]:
            warns.append("BG%d (toBg) is not defined in room %02X-%02X" % (z["toBg"], z["area"], z["room"]))
    return errs, warns


def apply_request(text, req, rooms=None):
    """Apply {"set": [{area,room,bg,x,y,w,h,toBg}], "remove": [{area,room,bg,x,y,w,h}]} to the file's text. rooms (see
    check_zone) enables the semantic checks of the zones being set; None skips them. Returns (new text, report); the
    report carries "warnings". A missing file is `text == None`: the result starts with HEADER."""
    if not isinstance(req, dict):
        raise ZoneError("request is not an object")
    sets, removes = req.get("set") or [], req.get("remove") or []
    if not isinstance(sets, list) or not isinstance(removes, list) or len(sets) + len(removes) > 2000:
        raise ZoneError("bad request")
    warnings = []
    for s in sets:
        validate_key(s)
        _int(s.get("toBg"), 1, 3, "toBg")
        errs, warns = check_zone(s, rooms)
        if errs:
            raise ZoneError("%s: %s" % (Doc._render(s), "; ".join(errs)))
        warnings += ["%s: %s" % (Doc._render(s), w) for w in warns]
    for r in removes:
        validate_key(r)
    new_file = text is None
    doc = Doc(HEADER if new_file else text)
    rep = doc.apply(sets, [key_of(r) for r in removes])
    rep["warnings"] = warnings
    return doc.text(), rep


def to_json(text, rel):
    doc = Doc(text)
    return json.dumps({"path": rel, "entries": doc.public(), "unmanaged": doc.unmanaged})


write_atomic = depth_inc.write_atomic


def rooms_from_maps(maps):
    """The `rooms` argument of check_zone from a parsed maps.json."""
    out = {}
    for a in maps["areas"]:
        for r in a["rooms"]:
            out[(a["id"], r["id"])] = {"width": r["width"], "height": r["height"],
                                       "layers": {int(bg[2:]) for bg, L in r["layers"].items() if L.get("data")}}
    return out
