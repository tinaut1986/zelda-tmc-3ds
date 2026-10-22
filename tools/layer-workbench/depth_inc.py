"""Reader and writer of platform/3ds/source/port_sprite_depth.inc, the port's per-entity stereo depth list.

    PORT_SPRITE_DEPTH(kind, id, type, mode)      type: a number or PORT_ANY_TYPE
    mode: PORT_DEPTH_UP | PORT_DEPTH_DOWN (= PORT_DEPTH_INVERT) | PORT_DEPTH_LEFT | PORT_DEPTH_RIGHT | PORT_DEPTH_FLAT

The file is hand-written and shared with other people (and other tools), so the writer is a minimal-diff editor,
not a serialiser: it keeps every line it does not manage (the header block, comments, blank lines, entries it does
not understand) byte for byte, and only adds, replaces or removes PORT_SPRITE_DEPTH lines on request.

  - a line it parses is an entry; anything else is left alone, including entries inside a block comment and entries
    whose type or mode it cannot read (they are reported as `unmanaged`);
  - the comment lines directly above an entry (`//` or a one-line `/* */`, no blank line between, and not the file's
    first line) are attached to it: removing the entry removes them, changing it keeps them. A multi-line block
    comment is never attached (it is the header);
  - a changed entry keeps its indentation and its trailing comment; INVERT stays INVERT unless the mode changes, and
    a mode this module writes is spelled PORT_DEPTH_DOWN;
  - a new entry goes at the end of the entries; an entry for one exact type goes just before the first entry of the
    same kind and id that matches every type, because the port takes the FIRST entry that matches
    (SpriteDepthMode in port/port_draw.c): placed after it, the exact entry would never be reached;
  - the line ending of the file (LF or CRLF) and its final newline are kept.

Operations are a three-way edit: the caller sends what to set and what to remove (computed against the entries it
loaded), and they are applied to whatever the file holds now, so a change made to the file meanwhile survives.
"""
import json
import os
import re

MODES = ("UP", "DOWN", "LEFT", "RIGHT", "FLAT")
WRITE_MODE = {m: "PORT_DEPTH_" + m for m in MODES}
READ_MODE = {"PORT_DEPTH_UP": "UP", "PORT_DEPTH_DOWN": "DOWN", "PORT_DEPTH_INVERT": "DOWN",
             "PORT_DEPTH_LEFT": "LEFT", "PORT_DEPTH_RIGHT": "RIGHT", "PORT_DEPTH_FLAT": "FLAT"}
ANY = "PORT_ANY_TYPE"
ANY_VALUE = 0xFF            # PORT_ANY_TYPE: no real entity type uses it

_ENTRY = re.compile(r"^(?P<ind>\s*)PORT_SPRITE_DEPTH\(\s*(?P<kind>\w+)\s*,\s*(?P<id>\w+)\s*,\s*(?P<type>\w+)\s*,"
                    r"\s*(?P<mode>\w+)\s*\)(?P<tail>\s*(?://.*|/\*.*?\*/)?)\s*$")
_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")


class DepthError(ValueError):
    pass


def key_of(e):
    """(kind, id, type) with type None for PORT_ANY_TYPE: what identifies an entry."""
    return (e["kind"], e["id"], e["type"])


def _classify(lines):
    """Per line: 'entry' (candidate), 'comment' (a one-line comment), 'block' (inside or closing a multi-line
    comment), or 'other'."""
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
        elif "PORT_SPRITE_DEPTH(" in s:
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
            t = m.group("type") if m else None
            ok = m and m.group("mode") in READ_MODE and (t == ANY or re.fullmatch(r"\d+|0x[0-9a-fA-F]+", t))
            if not ok or (t != ANY and int(t, 0) >= ANY_VALUE):
                self.unmanaged.append({"line": i + 1, "text": ln.strip()})
                continue
            j = i
            while j > 1 and kinds[j - 1] == "comment":      # j > 1: never the file's first line
                j -= 1
            self.entries.append({
                "line": i + 1, "kind": m.group("kind"), "id": m.group("id"),
                "type": None if t == ANY else int(t, 0), "mode": READ_MODE[m.group("mode")],
                "spelling": m.group("mode"), "tail": m.group("tail").strip(),
                "comment": [x.strip() for x in self.lines[j:i]], "_first": j,
            })

    # ---- queries
    def find(self, key):
        return [e for e in self.entries if key_of(e) == key]

    def public(self):
        out = []
        for e in self.entries:
            d = {k: v for k, v in e.items() if not k.startswith("_")}
            out.append(d)
        return out

    def text(self):
        return self.eol.join(self.lines) + (self.eol if self.final_newline and self.lines else "")

    # ---- edits
    def apply(self, set_=(), remove=()):
        """Returns {"added": n, "changed": n, "removed": n, "commentLinesRemoved": n}. Mutates the document;
        entries and line numbers are re-read afterwards."""
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
            mode = s["mode"]
            found = self.find(k)
            if found:
                for e in found:
                    if e["mode"] == mode:
                        continue
                    i = e["line"] - 1
                    self.lines[i] = _ENTRY.sub(lambda m: m.group("ind") + self._render(s) + m.group("tail"), self.lines[i])
                    rep["changed"] += 1
                self._reparse()
                continue
            self.lines.insert(self._insert_at(k), self._render(s))
            rep["added"] += 1
            self._reparse()
        if any(rep.values()) and self.lines and not self.final_newline:
            self.final_newline = True
        return rep

    def _render(self, e):
        t = ANY if e["type"] is None else str(e["type"])
        return "PORT_SPRITE_DEPTH(%s, %s, %s, %s)" % (e["kind"], e["id"], t, WRITE_MODE[e["mode"]])

    def _insert_at(self, k):
        kind, id_, t = k
        if t is not None:
            for e in self.entries:
                if e["kind"] == kind and e["id"] == id_ and e["type"] is None:
                    return e["_first"]
        if self.entries:
            return self.entries[-1]["line"]
        return len(self.lines)

    def _reparse(self):
        d = Doc(self.eol.join(self.lines) + (self.eol if self.final_newline and self.lines else ""))
        self.entries, self.unmanaged = d.entries, d.unmanaged


def parse(text):
    return Doc(text)


def validate(op):
    """A set/remove item from the client: kind and id are C identifiers, type is None or 0..254, mode is known."""
    if not isinstance(op, dict):
        raise DepthError("entry is not an object")
    for f in ("kind", "id"):
        if not isinstance(op.get(f), str) or not _NAME.match(op[f]):
            raise DepthError("bad %s %r" % (f, op.get(f)))
    t = op.get("type")
    if t is not None and (not isinstance(t, int) or isinstance(t, bool) or not 0 <= t < ANY_VALUE):
        raise DepthError("bad type %r (0..254 or null for any)" % (t,))
    return op


def apply_request(text, req):
    """Apply a client request {"set": [{kind,id,type,mode}], "remove": [{kind,id,type}]} to the file's text.
    Returns (new text, report)."""
    if not isinstance(req, dict):
        raise DepthError("request is not an object")
    sets, removes = req.get("set") or [], req.get("remove") or []
    if not isinstance(sets, list) or not isinstance(removes, list) or len(sets) + len(removes) > 2000:
        raise DepthError("bad request")
    for s in sets:
        validate(s)
        if s.get("mode") not in MODES:
            raise DepthError("bad mode %r" % (s.get("mode"),))
    for r in removes:
        validate(r)
    doc = Doc(text)
    rep = doc.apply(sets, [key_of(r) for r in removes])
    return doc.text(), rep


def effective(entries, kind, id_, type_):
    """The mode the port gives an entity: the FIRST entry (file order) of its kind and id whose type is
    PORT_ANY_TYPE or equals the entity's. Returns (mode, entry or None)."""
    for e in entries:
        if e["kind"] == kind and e["id"] == id_ and (e["type"] is None or e["type"] == type_):
            return e["mode"], e
    return "UP", None


def write_atomic(path, data, binary=False):
    """tmp file in the same folder, then rename: a half-written list would break the port's build."""
    tmp = path + ".tmp"
    with open(tmp, "wb" if binary else "w", **({} if binary else {"encoding": "utf-8", "newline": ""})) as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    try:
        os.chmod(tmp, os.stat(path).st_mode & 0o7777)
    except OSError:
        pass
    os.replace(tmp, path)


def to_json(text, rel):
    doc = Doc(text)
    return json.dumps({"path": rel, "entries": doc.public(), "unmanaged": doc.unmanaged})
