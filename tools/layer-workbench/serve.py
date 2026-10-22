#!/usr/bin/env python3
"""Serves the layer workbench and opens it in the browser.

    python3 tools/layer-workbench/serve.py [--rom PATH] [--port 8765] [--no-browser] [--rebuild]
                                           [--depth-inc PATH] [--sprite-view PATH]

maps.json is built from the ROM (TMC_ROM or --rom) when it is missing or older
than build_maps.py / tmcdata.py / sprites.py (the same build writes sprites.json).
A maps.json.gz alone is accepted too. Python 3 standard library only. maps.json
and sprites.json are the game's art: they are served to 127.0.0.1 only and must
never be committed or shared.

Two files are written back, through two endpoints and nothing else:

  /sprite-view   GET / POST   sprite_view.json: the sprite picker's choices (which animation, frame, flips a
                              kind of entity is previewed with). Choices only, no art; meant to be committed.
                              Changes nothing in the game.
  /sprite-depth  GET / POST   platform/3ds/source/port_sprite_depth.inc: the port's stereo depth list, edited by
                              the viewer's 3D DEPTH panel (depth_inc.py keeps comments and unknown lines).
                              --depth-inc PATH points both at another file (the tests use a temp copy).

Both writes are atomic (temp file, then rename), accept only application/json from the page this server serves
(Host must be 127.0.0.1/localhost), and are capped at 1 MB.
"""
import argparse
import functools
import http.server
import json
import os
import subprocess
import sys
import threading
import webbrowser

import depth_inc

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PORT = 8765
DEPTH_INC = os.path.join(ROOT, "platform", "3ds", "source", "port_sprite_depth.inc")
SPRITE_VIEW = os.path.join(HERE, "sprite_view.json")
MAX_BODY = 1 << 20
SPRITE_VIEW_FORMAT = "tmc-sprite-view"


def mtime(p):
    return os.path.getmtime(p) if os.path.isfile(p) else 0


def maps_fresh():
    src = max(mtime(os.path.join(HERE, n)) for n in ("build_maps.py", "tmcdata.py", "sprites.py", "entdefs.py"))
    have = max(mtime(os.path.join(HERE, "maps.json")), mtime(os.path.join(HERE, "maps.json.gz")))
    # sprites.json is written by the same build; the viewer works without it (markers only)
    return have and have >= src and mtime(os.path.join(HERE, "sprites.json")) >= src


def ensure_maps(rom, force):
    if maps_fresh() and not force:
        return True
    rom = rom or os.environ.get("TMC_ROM")
    if not rom or not os.path.isfile(rom):
        if mtime(os.path.join(HERE, "maps.json")) or mtime(os.path.join(HERE, "maps.json.gz")):
            print("maps.json / sprites.json are older than the extractor but no ROM was given (--rom / TMC_ROM): using them as they are.")
            return True
        print("maps.json is missing: give the ROM with --rom PATH or TMC_ROM to build it.", file=sys.stderr)
        return False
    print("Building maps.json from", rom)
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_maps.py"), "--rom", rom])
    return r.returncode == 0


def empty_sprite_view():
    return {"format": SPRITE_VIEW_FORMAT, "version": 1, "rules": [], "entities": []}


def check_sprite_view(d):
    """The shape of sprite_view.json: {format, version, rules: [...], entities: [...]}; each item names what it
    applies to and the choice. Raises ValueError."""
    if not isinstance(d, dict) or d.get("format") != SPRITE_VIEW_FORMAT or d.get("version") != 1:
        raise ValueError("not a %s v1 document" % SPRITE_VIEW_FORMAT)
    ok_int = lambda v, lo, hi: v is None or (isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi)
    for r in d.get("rules", []):
        if not (isinstance(r, dict) and isinstance(r.get("kind"), str) and isinstance(r.get("id"), str)
                and ok_int(r.get("type"), 0, 254)):
            raise ValueError("bad rule %r" % (r,))
    for e in d.get("entities", []):
        if not (isinstance(e, dict) and isinstance(e.get("room"), str) and ok_int(e.get("index"), 0, 4095)):
            raise ValueError("bad entity entry %r" % (e,))
    for x in list(d.get("rules", [])) + list(d.get("entities", [])):
        if not (ok_int(x.get("anim"), 0, 255) and ok_int(x.get("step"), 0, 255) and ok_int(x.get("frame"), 0, 255)
                and x.get("flipX", 0) in (0, 1) and x.get("flipY", 0) in (0, 1) and x.get("rot", 0) in (0, 90, 180, 270)):
            raise ValueError("bad choice %r" % (x,))
    return {"format": SPRITE_VIEW_FORMAT, "version": 1, "rules": d.get("rules", []), "entities": d.get("entities", [])}


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                          **{".json": "application/json", ".gz": "application/gzip", ".js": "text/javascript"})
    depth_inc = DEPTH_INC
    sprite_view = SPRITE_VIEW

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        if "--verbose" in sys.argv:
            super().log_message(fmt, *args)

    # ---- the two endpoints
    def _send(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _err(self, code, msg):
        self._send(code, json.dumps({"error": msg}))

    def _local(self):
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]")
        return host in ("127.0.0.1", "localhost", "::1")

    def _body_json(self):
        if not self._local():
            self._err(403, "only from 127.0.0.1")
            return None
        if (self.headers.get("Content-Type") or "").split(";")[0].strip() != "application/json":
            self._err(415, "Content-Type must be application/json")
            return None
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY:
            self._err(413, "too large")
            return None
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except ValueError as e:
            self._err(400, "bad JSON: %s" % e)
            return None

    def _depth_json(self, extra=None):
        p = self.depth_inc
        rel = os.path.relpath(p, ROOT) if p.startswith(ROOT) else p
        if os.path.isfile(p):
            with open(p, encoding="utf-8", newline="") as f:
                d = json.loads(depth_inc.to_json(f.read(), rel))
            d["exists"] = True
        else:
            d = {"path": rel, "entries": [], "unmanaged": [], "exists": False}
        d.update(extra or {})
        return json.dumps(d)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/sprite-depth":
            return self._send(200, self._depth_json())
        if path == "/sprite-view":
            if os.path.isfile(self.sprite_view):
                with open(self.sprite_view, "rb") as f:
                    return self._send(200, f.read())
            return self._send(200, json.dumps(empty_sprite_view()))
        return super().do_GET()

    def do_POST(self):
        path = self.path.split("?")[0]
        if path not in ("/sprite-depth", "/sprite-view"):
            return self._err(404, "only /sprite-depth and /sprite-view accept POST")
        req = self._body_json()
        if req is None:
            return
        if path == "/sprite-view":
            try:
                doc = check_sprite_view(req)
            except ValueError as e:
                return self._err(400, str(e))
            doc["rules"].sort(key=lambda r: (r["kind"], r["id"], -1 if r.get("type") is None else r["type"]))
            doc["entities"].sort(key=lambda e: (e["room"], e["index"]))
            depth_inc.write_atomic(self.sprite_view, json.dumps(doc, indent=1, sort_keys=True) + "\n")
            return self._send(200, json.dumps({"saved": True, "rules": len(doc["rules"]), "entities": len(doc["entities"])}))
        p = self.depth_inc
        text = ""
        if os.path.isfile(p):
            with open(p, encoding="utf-8", newline="") as f:
                text = f.read()
        try:
            new, rep = depth_inc.apply_request(text, req)
        except depth_inc.DepthError as e:
            return self._err(400, str(e))
        if new != text:
            os.makedirs(os.path.dirname(os.path.abspath(p)), exist_ok=True)
            depth_inc.write_atomic(p, new)
            print("sprite depth: %s -> %s" % (rep, os.path.relpath(p, ROOT) if p.startswith(ROOT) else p), flush=True)
        return self._send(200, self._depth_json({"report": rep, "changed": new != text}))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--rebuild", action="store_true", help="rebuild maps.json even if it looks current")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--depth-inc", default=DEPTH_INC, help="the stereo depth list /sprite-depth reads and writes "
                    "(default: platform/3ds/source/port_sprite_depth.inc)")
    ap.add_argument("--sprite-view", default=SPRITE_VIEW, help="the sprite picker's choices /sprite-view reads and "
                    "writes (default: sprite_view.json next to this script)")
    a = ap.parse_args()
    if not ensure_maps(a.rom, a.rebuild):
        sys.exit(1)
    Handler.depth_inc = os.path.abspath(a.depth_inc)
    Handler.sprite_view = os.path.abspath(a.sprite_view)
    handler = functools.partial(Handler, directory=HERE)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
    except OSError as e:
        sys.exit("cannot listen on port %d: %s" % (a.port, e))
    url = "http://127.0.0.1:%d/" % a.port
    print("Layer workbench at", url, "(Ctrl+C to stop)")
    print("  /sprite-depth writes", Handler.depth_inc)
    print("  /sprite-view  writes", Handler.sprite_view, flush=True)
    if not a.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
