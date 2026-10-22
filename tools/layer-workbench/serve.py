#!/usr/bin/env python3
"""Serves the layer workbench and opens it in the browser.

    python3 tools/layer-workbench/serve.py [--rom PATH] [--port 8765] [--no-browser] [--rebuild]

maps.json is built from the ROM (TMC_ROM or --rom) when it is missing or older
than build_maps.py / tmcdata.py / sprites.py (the same build writes sprites.json).
A maps.json.gz alone is accepted too. Python 3 standard library only. maps.json
and sprites.json are the game's art: they are served to 127.0.0.1 only and must
never be committed or shared.
"""
import argparse
import functools
import http.server
import os
import subprocess
import sys
import threading
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8765


def mtime(p):
    return os.path.getmtime(p) if os.path.isfile(p) else 0


def maps_fresh():
    src = max(mtime(os.path.join(HERE, n)) for n in ("build_maps.py", "tmcdata.py", "sprites.py"))
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


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                          **{".json": "application/json", ".gz": "application/gzip", ".js": "text/javascript"})

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        if "--verbose" in sys.argv:
            super().log_message(fmt, *args)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--rebuild", action="store_true", help="rebuild maps.json even if it looks current")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if not ensure_maps(a.rom, a.rebuild):
        sys.exit(1)
    handler = functools.partial(Handler, directory=HERE)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
    except OSError as e:
        sys.exit("cannot listen on port %d: %s" % (a.port, e))
    url = "http://127.0.0.1:%d/" % a.port
    print("Layer workbench at", url, "(Ctrl+C to stop)", flush=True)
    if not a.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
