#!/usr/bin/env python3
"""Checks of zone_inc.py (the reader/writer of port_zone_depth.inc) and of serve.py's /zone-depth endpoint.

    python3 tools/layer-workbench/test_zone_inc.py

Nothing here touches the port's real port_zone_depth.inc: every file written is a temp copy (the real file is only
hashed before and after as a guard; it may not exist, which is fine).
"""
import functools
import hashlib
import http.client
import http.server
import json
import os
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import serve  # noqa: E402
import zone_inc as Z  # noqa: E402

REAL = serve.ZONE_INC
CHECKS, FAILS = [0], []

ROOMS = {(2, 0): {"width": 1008, "height": 960, "layers": {1, 2}}, (0x20, 0): {"width": 256, "height": 160, "layers": {2}}}


def check(cond, msg):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(msg)
    return cond


def equal(got, want, msg):
    return check(got == want, "%s: got %r, want %r" % (msg, got, want))


def sha(p):
    return hashlib.sha1(open(p, "rb").read()).hexdigest() if os.path.isfile(p) else None


def z(area=2, room=0, bg=2, x=272, y=864, w=16, h=16, to=1):
    return {"area": area, "room": room, "bg": bg, "x": x, "y": y, "w": w, "h": h, "toBg": to}


def key(d):
    return {k: d[k] for k in Z.FIELDS}


SAMPLE = """/* Header block.
 *
 * PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)   <- inside the header: never an entry
 */
PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)
// the log hut's neighbour
/* second line */
  PORT_ZONE_DEPTH(2, 0, 2, 0x120, 864, 16, 16, 1)   // trailing note

#if 0
a line the tool does not understand
#endif
PORT_ZONE_DEPTH(0x20, 0x00, BG2, 0, 0, 16, 16, 1)
PORT_ZONE_DEPTH(0x20, 0x00, 2, 08, 0, 16, 16, 1)
PORT_ZONE_DEPTH(0x20, 0x00, 2, 0, 0, 16, 16)
/*
PORT_ZONE_DEPTH(0x03, 0x00, 2, 0, 0, 16, 16, 1)
*/
PORT_ZONE_DEPTH(0x20, 0x00, 2, 16, 16, 32, 32, 3)
"""


def test_parse():
    print("parse")
    d = Z.parse(SAMPLE)
    equal([(e["area"], e["room"], e["bg"], e["x"], e["y"], e["w"], e["h"], e["toBg"]) for e in d.entries],
          [(2, 0, 2, 272, 864, 16, 16, 1), (2, 0, 2, 288, 864, 16, 16, 1), (0x20, 0, 2, 16, 16, 32, 32, 3)],
          "entries read, hex and decimal (header, block comment and bad lines are not entries)")
    equal(len(d.unmanaged), 3, "a symbolic bg, a leading-zero number and a missing argument are reported, not managed")
    equal(d.entries[1]["comment"], ["// the log hut's neighbour", "/* second line */"], "comments directly above are attached")
    equal(d.entries[0]["comment"], [], "the header is not attached to the first entry")
    equal(d.entries[1]["tail"], "// trailing note", "trailing comment kept")


def test_roundtrip_edits():
    print("round trip and edits")
    for name, text in (("sample", SAMPLE), ("empty", ""), ("no final newline", "PORT_ZONE_DEPTH(2, 0, 2, 0, 0, 16, 16, 1)"),
                       ("CRLF", SAMPLE.replace("\n", "\r\n")), ("blank", "\n\n")):
        equal(Z.parse(text).text(), text, "parse -> text is the identity: " + name)
        new, rep = Z.apply_request(text, {}, ROOMS)
        equal((new, rep["added"] + rep["changed"] + rep["removed"]), (text, 0), "an empty request changes nothing: " + name)
    new, rep = Z.apply_request(SAMPLE, {"set": [z(to=1)]}, ROOMS)
    equal((new, rep["changed"]), (SAMPLE, 0), "setting what is already there is a no-op")
    # replace toBg: only that number moves; indentation, number spelling and trailing comment stay
    new, rep = Z.apply_request(SAMPLE, {"set": [z(x=288, to=3)]}, ROOMS)
    check("  PORT_ZONE_DEPTH(2, 0, 2, 0x120, 864, 16, 16, 3)   // trailing note\n" in new, "toBg changed in place, spelling kept")
    equal(rep["changed"], 1, "one change")
    equal(new.replace("16, 16, 3)   //", "16, 16, 1)   //"), SAMPLE, "nothing else moved")
    # remove with the attached comment lines
    new, rep = Z.apply_request(SAMPLE, {"remove": [key(z(x=288))]}, ROOMS)
    equal((rep["removed"], rep["commentLinesRemoved"]), (1, 2), "removed with its two comment lines")
    check("neighbour" not in new and "second line" not in new and "Header block" in new and "0x02, 0x00, 2, 272" in new, "nothing else went")
    new, _ = Z.apply_request(SAMPLE, {"remove": [key(z(x=272))]})
    check(new.startswith("/* Header block") and " */\n// the log hut" in new, "removing the first entry keeps the header")
    equal(Z.apply_request(SAMPLE, {"remove": [key(z(x=1))]})[0], SAMPLE, "removing a missing entry is a no-op")
    # add: after the last entry of the same room, else after the last entry
    new, rep = Z.apply_request(SAMPLE, {"set": [z(x=304)]}, ROOMS)
    lines = new.split("\n")
    i = lines.index("PORT_ZONE_DEPTH(0x02, 0x00, 2, 304, 864, 16, 16, 1)")
    equal(lines[i - 1].strip(), "PORT_ZONE_DEPTH(2, 0, 2, 0x120, 864, 16, 16, 1)   // trailing note".strip(), "a new zone follows the last one of its room")
    equal(new.replace("PORT_ZONE_DEPTH(0x02, 0x00, 2, 304, 864, 16, 16, 1)\n", ""), SAMPLE, "an add only adds one line")
    new, _ = Z.apply_request(SAMPLE, {"set": [z(area=0x20, bg=2, x=0, y=0, w=8, h=8, to=1)]}, ROOMS)
    equal(Z.parse(new).entries[-1]["w"], 8, "a zone of the last room goes at the end")
    new, _ = Z.apply_request("/* only a header */", {"set": [z()]}, ROOMS)
    equal(new, "/* only a header */\nPORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)\n", "added to a header-only file, no final newline")
    new, _ = Z.apply_request("", {"set": [z()]}, ROOMS)
    equal(new, "PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)\n", "added to an empty file")
    # a missing file (None) starts with the header
    new, rep = Z.apply_request(None, {"set": [z()]}, ROOMS)
    check(new.startswith("/* Hand-curated stereo 3D \"zone depth fixes\".") and new.endswith(" */\nPORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)\n"), "a new file: header, then the entry")
    check("PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)" in Z.HEADER and "0x02, 0x00, 2, 272, 864, 16, 16, 1" in Z.HEADER, "the header describes the format and the example")
    equal([e["line"] for e in Z.parse(new).entries], [len(new.split("\n")) - 1], "the header's own text holds no entry")
    equal(Z.parse(Z.HEADER).entries, [], "the header alone has no entries")
    # CRLF
    new, _ = Z.apply_request(SAMPLE.replace("\n", "\r\n"), {"set": [z(x=304)]}, ROOMS)
    check("\n" not in new.replace("\r\n", ""), "CRLF kept")
    # three-way: a line added meanwhile survives
    other = SAMPLE + "PORT_ZONE_DEPTH(0x20, 0x00, 2, 64, 64, 16, 16, 1)\n"
    new, _ = Z.apply_request(other, {"set": [z(x=288, to=3)]}, ROOMS)
    check("64, 64, 16, 16, 1)" in new and "16, 16, 3)   //" in new, "changes made to the file meanwhile survive")
    # an unmanaged line is never touched by an edit
    new, _ = Z.apply_request(SAMPLE, {"set": [z(x=304)], "remove": [key(z(x=272))]}, ROOMS)
    equal([u["text"] for u in Z.parse(new).unmanaged], [u["text"] for u in Z.parse(SAMPLE).unmanaged], "unmanaged lines survive")


def test_validation():
    print("validation")
    def refused(req, why):
        try:
            Z.apply_request(SAMPLE, req, ROOMS)
            check(False, "accepted: " + why)
        except Z.ZoneError:
            check(True, "")
    refused({"set": [z(area=9)]}, "unknown room")
    refused({"set": [z(room=5)]}, "unknown room number")
    refused({"set": [z(x=1000, w=16)]}, "outside to the right")
    refused({"set": [z(y=950, h=16)]}, "outside below")
    refused({"set": [z(bg=1, to=1)]}, "bg == toBg")
    refused({"set": [z(bg=0)]}, "bg 0")
    refused({"set": [z(to=4)]}, "toBg 4")
    refused({"set": [z(w=0)]}, "empty rectangle")
    refused({"set": [z(x=-1)]}, "negative x")
    refused({"set": [dict(z(), x="1")]}, "text x")
    refused({"set": [dict(z(), x=True)]}, "bool x")
    refused({"set": [{"area": 2}]}, "missing fields")
    refused({"remove": [dict(z(), bg=7)]}, "bad key in remove")
    refused({"set": "x"}, "set not a list")
    # a layer the room does not define is a warning, not an error
    new, rep = Z.apply_request(SAMPLE, {"set": [z(area=0x20, bg=1, to=2, x=0, y=0)]}, ROOMS)
    check(len(rep["warnings"]) == 1 and "BG1 is not defined" in rep["warnings"][0] and "PORT_ZONE_DEPTH(0x20, 0x00, 1, 0, 0, 16, 16, 2)" in new, "an undefined layer warns and still saves")
    equal(Z.apply_request(SAMPLE, {"set": [z(x=320)]}, ROOMS)[1]["warnings"], [], "no warning for a defined layer")
    new, rep = Z.apply_request(SAMPLE, {"set": [z(area=9)]})
    check("0x09" in new, "without a room table the semantic checks are skipped")
    check(Z.check_zone(z(x=992, w=16), ROOMS)[0] == [] and Z.check_zone(z(x=993, w=16), ROOMS)[0] != [], "the rectangle may touch the room's edge")


def test_server():
    print("server endpoint (temp copies)")
    before = sha(REAL)
    with tempfile.TemporaryDirectory() as tmp:
        inc = os.path.join(tmp, "zones", "zone.inc")        # not there yet: the POST creates it and its folder
        serve.Handler.zone_inc = inc
        serve.Handler.zone_rooms = staticmethod(lambda: ROOMS)
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(serve.Handler, directory=HERE))
        port = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()

        def req(method, path, body=None, ctype="application/json", host=None):
            c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            h = {"Content-Type": ctype}
            if host:
                h["Host"] = host
            c.request(method, path, None if body is None else json.dumps(body), h)
            r = c.getresponse()
            data = r.read()
            c.close()
            return r.status, data

        d = json.loads(req("GET", "/zone-depth")[1])
        equal((d["exists"], d["entries"], d["unmanaged"]), (False, [], []), "GET with no file")
        st, data = req("POST", "/zone-depth", {"set": [z()]})
        d = json.loads(data)
        equal((st, d["changed"], d["report"]["added"], d["exists"], len(d["entries"])), (200, True, 1, True, 1), "POST creates the file")
        text = open(inc, newline="").read()
        check(text.startswith("/* Hand-curated") and text.endswith("PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)\n"), "the new file has the header and the entry")
        check(not os.path.exists(inc + ".tmp"), "no temp file left")
        e = json.loads(req("GET", "/zone-depth")[1])["entries"][0]
        equal((e["area"], e["room"], e["bg"], e["x"], e["y"], e["w"], e["h"], e["toBg"]), (2, 0, 2, 272, 864, 16, 16, 1), "GET returns the entry")
        # edit: change toBg, add another, a warning comes back
        st, data = req("POST", "/zone-depth", {"set": [z(to=3), z(area=0x20, bg=1, to=2, x=0, y=0)]})
        d = json.loads(data)
        equal((st, d["report"]["changed"], d["report"]["added"], len(d["report"]["warnings"])), (200, 1, 1, 2), "POST edit: changed, added, two warnings (BG3 of 02-00, BG1 of 20-00)")
        # a file edited by hand meanwhile survives the next POST
        with open(inc, "a", newline="") as f:
            f.write("// by hand\nPORT_ZONE_DEPTH(0x20, 0x00, 2, 64, 64, 16, 16, 1)\n")
        req("POST", "/zone-depth", {"remove": [key(z())]})
        text = open(inc, newline="").read()
        check("by hand" in text and "64, 64, 16, 16, 1)" in text and not [e for e in Z.parse(text).entries if e["area"] == 2] and "Hand-curated" in text, "remove applied to the file as it is now")
        # refusals
        after = open(inc, newline="").read()
        equal(req("POST", "/zone-depth", {"set": [z(x=2000)]})[0], 400, "a rectangle outside the room is refused")
        equal(req("POST", "/zone-depth", {"set": [z(bg=1, to=1)]})[0], 400, "bg == toBg is refused")
        equal(req("POST", "/zone-depth", {}, ctype="text/plain")[0], 415, "not JSON")
        equal(req("POST", "/zone-depth", {}, host="evil.example")[0], 403, "foreign Host")
        equal(req("POST", "/index.html", {})[0], 404, "no other path accepts a POST")
        equal(open(inc, newline="").read(), after, "refused requests wrote nothing")
        d = json.loads(req("POST", "/zone-depth", {})[1])
        equal(d["changed"], False, "an empty POST writes nothing")
        srv.shutdown()
        # the real maps.json, when present: Hyrule Town and the first example
        maps = os.path.join(HERE, "maps.json")
        if os.path.isfile(maps):
            rooms = Z.rooms_from_maps(json.load(open(maps)))
            equal((rooms[(2, 0)]["width"], rooms[(2, 0)]["height"], rooms[(2, 0)]["layers"] >= {1, 2}), (1008, 960, True), "maps.json: Hyrule Town 1008x960 with BG1 and BG2")
            new, rep = Z.apply_request(None, {"set": [z()]}, rooms)
            equal(rep["warnings"], [], "the owner's first example validates against the ROM")
    equal(sha(REAL), before, "the real port_zone_depth.inc was not touched")


def main():
    test_parse()
    test_roundtrip_edits()
    test_validation()
    test_server()
    print()
    if FAILS:
        for f in FAILS[:40]:
            print("FAIL", f)
        print("%d of %d checks failed" % (len(FAILS), CHECKS[0]))
        return 1
    print("all %d checks passed" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
