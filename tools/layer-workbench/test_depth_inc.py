#!/usr/bin/env python3
"""Checks of depth_inc.py (the reader/writer of port_sprite_depth.inc) and of serve.py's two write endpoints.

    python3 tools/layer-workbench/test_depth_inc.py

Nothing here touches the port's real .inc or the committed sprite_view.json: every file written is a temp copy
(the real .inc is only READ, as a realistic sample, and its hash is compared before and after as a guard).
"""
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
import depth_inc as D  # noqa: E402
import serve  # noqa: E402

REAL_INC = serve.DEPTH_INC
REAL_VIEW = serve.SPRITE_VIEW
CHECKS, FAILS = [0], []


def check(cond, msg):
    CHECKS[0] += 1
    if not cond:
        FAILS.append(msg)
    return cond


def equal(got, want, msg):
    return check(got == want, "%s: got %r, want %r" % (msg, got, want))


def sha(p):
    return hashlib.sha1(open(p, "rb").read()).hexdigest() if os.path.isfile(p) else None


SAMPLE = """/* Hand-curated stereo 3D depth corrections.
 *
 * PORT_SPRITE_DEPTH(kind, id, type, mode)   <- inside the header: never an entry
 *   mode  PORT_DEPTH_INVERT  hangs from its top edge
 */
PORT_SPRITE_DEPTH(OBJECT, BOSS_DOOR, PORT_ANY_TYPE, PORT_DEPTH_INVERT)
/* The big Chuchu's shadow: flat on the ground. */
// second line of its comment
PORT_SPRITE_DEPTH(OBJECT, CHUCHU_BOSS_START_PARTICLE, PORT_ANY_TYPE, PORT_DEPTH_FLAT)

#if 0
some line the tool does not understand
#endif
  PORT_SPRITE_DEPTH(ENEMY, OCTOROK, 2, PORT_DEPTH_DOWN)   // trailing note
PORT_SPRITE_DEPTH(NPC, ZELDA, SOME_SYMBOL, PORT_DEPTH_UP)
PORT_SPRITE_DEPTH(NPC, ZELDA, PORT_ANY_TYPE, PORT_DEPTH_SIDEWAYS)
/*
PORT_SPRITE_DEPTH(OBJECT, COMMENTED_OUT, PORT_ANY_TYPE, PORT_DEPTH_FLAT)
*/
"""


def test_parse():
    print("parse")
    d = D.parse(SAMPLE)
    keys = [(e["kind"], e["id"], e["type"], e["mode"]) for e in d.entries]
    equal(keys, [("OBJECT", "BOSS_DOOR", None, "DOWN"), ("OBJECT", "CHUCHU_BOSS_START_PARTICLE", None, "FLAT"),
                 ("ENEMY", "OCTOROK", 2, "DOWN")], "entries read (header, block comment and unknown lines are not entries)")
    equal([u["text"] for u in d.unmanaged], ["PORT_SPRITE_DEPTH(NPC, ZELDA, SOME_SYMBOL, PORT_DEPTH_UP)",
                                             "PORT_SPRITE_DEPTH(NPC, ZELDA, PORT_ANY_TYPE, PORT_DEPTH_SIDEWAYS)"],
          "entries it cannot read are reported, not managed")
    equal(d.entries[0]["spelling"], "PORT_DEPTH_INVERT", "INVERT is read as DOWN and remembers its spelling")
    equal(d.entries[1]["comment"], ["/* The big Chuchu's shadow: flat on the ground. */", "// second line of its comment"],
          "comment lines directly above an entry are attached to it")
    equal(d.entries[0]["comment"], [], "the header block is not attached to the first entry")
    equal(d.entries[2]["tail"], "// trailing note", "trailing comment kept")
    for mode, want in (("PORT_DEPTH_UP", "UP"), ("PORT_DEPTH_LEFT", "LEFT"), ("PORT_DEPTH_RIGHT", "RIGHT")):
        equal(D.parse("PORT_SPRITE_DEPTH(OBJECT, X, 0x1F, %s)\n" % mode).entries[0]["mode"], want, mode)
    equal(D.parse("PORT_SPRITE_DEPTH(OBJECT, X, 0x1F, PORT_DEPTH_UP)\n").entries[0]["type"], 31, "hex type")
    # first-match semantics of the port
    es = D.parse("PORT_SPRITE_DEPTH(OBJECT, A, PORT_ANY_TYPE, PORT_DEPTH_FLAT)\nPORT_SPRITE_DEPTH(OBJECT, A, 3, PORT_DEPTH_LEFT)\n"
                 "PORT_SPRITE_DEPTH(OBJECT, B, 3, PORT_DEPTH_LEFT)\nPORT_SPRITE_DEPTH(OBJECT, B, PORT_ANY_TYPE, PORT_DEPTH_DOWN)\n").entries
    equal(D.effective(es, "OBJECT", "A", 3)[0], "FLAT", "an ANY entry before the exact one wins (first match)")
    equal(D.effective(es, "OBJECT", "B", 3)[0], "LEFT", "an exact entry before the ANY one wins")
    equal(D.effective(es, "OBJECT", "B", 4)[0], "DOWN", "ANY covers the other types")
    equal(D.effective(es, "OBJECT", "C", 0), ("UP", None), "no entry: UP (default)")


def test_roundtrip():
    print("round trip")
    for name, text in (("sample", SAMPLE), ("empty", ""), ("one line, no newline", "PORT_SPRITE_DEPTH(OBJECT, X, 1, PORT_DEPTH_FLAT)"),
                       ("CRLF", SAMPLE.replace("\n", "\r\n")), ("blank only", "\n\n")):
        equal(D.parse(text).text(), text, "parse -> text is the identity: " + name)
        new, rep = D.apply_request(text, {})
        equal((new, sum(rep.values())), (text, 0), "an empty request changes nothing: " + name)
    # setting an entry to what it already is leaves INVERT spelled INVERT
    new, rep = D.apply_request(SAMPLE, {"set": [{"kind": "OBJECT", "id": "BOSS_DOOR", "type": None, "mode": "DOWN"}]})
    equal((new, rep["changed"]), (SAMPLE, 0), "INVERT stays INVERT when its mode does not change")
    if os.path.isfile(REAL_INC):
        real = open(REAL_INC, encoding="utf-8", newline="").read()
        equal(D.parse(real).text(), real, "the port's real .inc round-trips")
        check(len(D.parse(real).entries) >= 1, "the port's real .inc has entries")


def test_edits():
    print("edits")
    # replace: keeps the comment lines, the indentation and the trailing comment; INVERT -> DOWN spelling only when changed
    new, rep = D.apply_request(SAMPLE, {"set": [{"kind": "ENEMY", "id": "OCTOROK", "type": 2, "mode": "LEFT"}]})
    check("  PORT_SPRITE_DEPTH(ENEMY, OCTOROK, 2, PORT_DEPTH_LEFT)   // trailing note\n" in new, "replaced in place with indentation and trailing comment")
    equal(rep["changed"], 1, "one change reported")
    equal(new.replace("PORT_DEPTH_LEFT", "PORT_DEPTH_DOWN"), SAMPLE, "nothing else moved")
    new, rep = D.apply_request(SAMPLE, {"set": [{"kind": "OBJECT", "id": "BOSS_DOOR", "type": None, "mode": "LEFT"}]})
    check(new.startswith(SAMPLE[:SAMPLE.index("PORT_SPRITE_DEPTH(OBJECT, BOSS_DOOR")]) and "PORT_SPRITE_DEPTH(OBJECT, BOSS_DOOR, PORT_ANY_TYPE, PORT_DEPTH_LEFT)\n" in new,
          "a changed INVERT entry is rewritten with the new mode")
    new, _ = D.apply_request(SAMPLE, {"set": [{"kind": "OBJECT", "id": "BOSS_DOOR", "type": None, "mode": "DOWN"}]})
    equal(new, SAMPLE, "same mode: untouched")
    # remove: the entry and its attached comment lines, nothing else
    new, rep = D.apply_request(SAMPLE, {"remove": [{"kind": "OBJECT", "id": "CHUCHU_BOSS_START_PARTICLE", "type": None}]})
    equal(rep["removed"], 1, "removed one")
    equal(rep["commentLinesRemoved"], 2, "and its two attached comment lines")
    check("Chuchu" not in new and "second line" not in new and "BOSS_DOOR" in new and "trailing note" in new and "Hand-curated" in new,
          "removal left everything else")
    new, rep = D.apply_request(SAMPLE, {"remove": [{"kind": "OBJECT", "id": "BOSS_DOOR", "type": None}]})
    check(new.startswith("/* Hand-curated") and " */\n/* The big Chuchu" in new and rep["commentLinesRemoved"] == 0,
          "removing the first entry does not take the header with it")
    equal(D.apply_request(SAMPLE, {"remove": [{"kind": "OBJECT", "id": "NOPE", "type": None}]})[0], SAMPLE, "removing a missing entry is a no-op")
    # add: appended after the last entry; an exact type goes before the ANY entry of the same kind and id
    new, rep = D.apply_request(SAMPLE, {"set": [{"kind": "OBJECT", "id": "GENTARI_CURTAIN", "type": None, "mode": "LEFT"}]})
    lines = new.split("\n")
    i = lines.index("PORT_SPRITE_DEPTH(OBJECT, GENTARI_CURTAIN, PORT_ANY_TYPE, PORT_DEPTH_LEFT)")
    equal(lines[i - 1].strip(), "PORT_SPRITE_DEPTH(ENEMY, OCTOROK, 2, PORT_DEPTH_DOWN)   // trailing note".strip(),
          "a new entry goes right after the last managed entry")
    es = D.parse(new).entries
    equal(es[-1]["id"], "GENTARI_CURTAIN", "a new ANY entry is the last managed entry")
    equal([u["text"] for u in D.parse(new).unmanaged], [u["text"] for u in D.parse(SAMPLE).unmanaged], "unmanaged lines survive an add")
    equal(new.replace("PORT_SPRITE_DEPTH(OBJECT, GENTARI_CURTAIN, PORT_ANY_TYPE, PORT_DEPTH_LEFT)\n", ""), SAMPLE, "an add only adds one line")
    new, _ = D.apply_request(SAMPLE, {"set": [{"kind": "OBJECT", "id": "BOSS_DOOR", "type": 5, "mode": "UP"}]})
    es = D.parse(new).entries
    equal([(e["id"], e["type"]) for e in es][:2], [("BOSS_DOOR", 5), ("BOSS_DOOR", None)],
          "an exact entry is placed before the ANY entry of its kind and id (the port takes the first match)")
    equal(D.effective(es, "OBJECT", "BOSS_DOOR", 5)[0], "UP", "and it is the one that wins")
    # a file that has no entries, no trailing newline
    new, _ = D.apply_request("/* only a header */", {"set": [{"kind": "NPC", "id": "ZELDA", "type": 1, "mode": "FLAT"}]})
    equal(new, "/* only a header */\nPORT_SPRITE_DEPTH(NPC, ZELDA, 1, PORT_DEPTH_FLAT)\n", "added to a header-only file")
    new, _ = D.apply_request("", {"set": [{"kind": "NPC", "id": "ZELDA", "type": 1, "mode": "FLAT"}]})
    equal(new, "PORT_SPRITE_DEPTH(NPC, ZELDA, 1, PORT_DEPTH_FLAT)\n", "added to an empty file")
    # CRLF files stay CRLF
    crlf = SAMPLE.replace("\n", "\r\n")
    new, _ = D.apply_request(crlf, {"set": [{"kind": "NPC", "id": "ZELDA", "type": 1, "mode": "RIGHT"}]})
    check("\n" not in new.replace("\r\n", ""), "CRLF kept")
    # three-way: a line another editor added meanwhile survives
    other = SAMPLE + "PORT_SPRITE_DEPTH(OBJECT, ADDED_BY_SOMEONE_ELSE, PORT_ANY_TYPE, PORT_DEPTH_UP)\n"
    new, _ = D.apply_request(other, {"set": [{"kind": "ENEMY", "id": "OCTOROK", "type": 2, "mode": "RIGHT"}]})
    check("ADDED_BY_SOMEONE_ELSE" in new and "PORT_DEPTH_RIGHT" in new, "changes made to the file meanwhile survive")
    # validation
    for bad in ({"set": [{"kind": "object", "id": "X", "type": None, "mode": "UP"}]},
                {"set": [{"kind": "OBJECT", "id": "X; DROP", "type": None, "mode": "UP"}]},
                {"set": [{"kind": "OBJECT", "id": "X", "type": 255, "mode": "UP"}]},
                {"set": [{"kind": "OBJECT", "id": "X", "type": None, "mode": "INVERT"}]},
                {"set": [{"kind": "OBJECT", "id": "X", "type": "3", "mode": "UP"}]},
                {"remove": [{"kind": "OBJECT", "id": "", "type": None}]}, {"set": "x"}):
        try:
            D.apply_request(SAMPLE, bad)
            check(False, "accepted %r" % (bad,))
        except D.DepthError:
            check(True, "")


def test_server():
    print("server endpoints (temp copies)")
    before = (sha(REAL_INC), sha(REAL_VIEW))
    with tempfile.TemporaryDirectory() as tmp:
        inc, view = os.path.join(tmp, "depth.inc"), os.path.join(tmp, "view.json")
        with open(inc, "w", newline="") as f:
            f.write(SAMPLE)
        serve.Handler.depth_inc, serve.Handler.sprite_view = inc, view
        import functools
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

        st, data = req("GET", "/sprite-depth")
        d = json.loads(data)
        equal((st, len(d["entries"]), len(d["unmanaged"]), d["exists"]), (200, 3, 2, True), "GET /sprite-depth")
        st, data = req("POST", "/sprite-depth", {"set": [{"kind": "OBJECT", "id": "GENTARI_CURTAIN", "type": 0, "mode": "LEFT"}],
                                                   "remove": [{"kind": "ENEMY", "id": "OCTOROK", "type": 2}]})
        d = json.loads(data)
        equal((st, d["changed"], d["report"]["added"], d["report"]["removed"]), (200, True, 1, 1), "POST /sprite-depth")
        text = open(inc, newline="").read()
        check("GENTARI_CURTAIN, 0, PORT_DEPTH_LEFT" in text and "OCTOROK" not in text and "Hand-curated" in text, "the temp .inc holds the edit")
        check(not os.path.exists(inc + ".tmp"), "no temp file left behind")
        st, _ = req("POST", "/sprite-depth", {"set": [{"kind": "x", "id": "Y", "type": None, "mode": "UP"}]})
        equal(st, 400, "a bad entry is refused")
        st, _ = req("POST", "/sprite-depth", {}, ctype="text/plain")
        equal(st, 415, "a POST that is not JSON is refused")
        st, _ = req("POST", "/sprite-depth", {}, host="evil.example")
        equal(st, 403, "a foreign Host is refused")
        st, _ = req("POST", "/index.html", {})
        equal(st, 404, "no other path accepts a POST")
        after = open(inc, newline="").read()
        st, data = req("POST", "/sprite-depth", {})
        equal((json.loads(data)["changed"], open(inc, newline="").read()), (False, after), "an empty POST writes nothing")
        # missing file: GET says so, POST creates it
        os.remove(inc)
        d = json.loads(req("GET", "/sprite-depth")[1])
        equal((d["exists"], d["entries"]), (False, []), "GET with no file")
        req("POST", "/sprite-depth", {"set": [{"kind": "NPC", "id": "ZELDA", "type": None, "mode": "FLAT"}]})
        equal(open(inc).read(), "PORT_SPRITE_DEPTH(NPC, ZELDA, PORT_ANY_TYPE, PORT_DEPTH_FLAT)\n", "POST creates a missing file")
        # sprite view
        st, data = req("GET", "/sprite-view")
        equal((st, json.loads(data)["rules"]), (200, []), "GET /sprite-view with no file: an empty document")
        doc = {"format": "tmc-sprite-view", "version": 1,
               "rules": [{"kind": "OBJECT", "id": "B", "type": None, "anim": 1, "step": 0, "flipX": 1, "rot": 90},
                         {"kind": "ENEMY", "id": "A", "type": 3, "frame": 4}],
               "entities": [{"room": "20-00", "index": 1, "anim": 2}]}
        st, _ = req("POST", "/sprite-view", doc)
        equal(st, 200, "POST /sprite-view")
        saved = json.loads(open(view).read())
        equal([r["kind"] for r in saved["rules"]], ["ENEMY", "OBJECT"], "rules are stored sorted (small diffs)")
        equal(json.loads(req("GET", "/sprite-view")[1]), saved, "GET returns what was saved")
        for bad in ({"format": "x"}, dict(doc, rules=[{"kind": "A", "id": "B", "type": 999}]),
                    dict(doc, entities=[{"room": "20-00", "index": 0, "rot": 45}])):
            equal(req("POST", "/sprite-view", bad)[0], 400, "a bad sprite view is refused")
        srv.shutdown()
    equal((sha(REAL_INC), sha(REAL_VIEW)), before, "the real .inc and sprite_view.json were not touched")


def main():
    test_parse()
    test_roundtrip()
    test_edits()
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
