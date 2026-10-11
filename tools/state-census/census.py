#!/usr/bin/env python3
"""Where a save state holds host pointers (PLAN P1.8b).

    census.py STATE.bin BUILD.elf [--min N] [--detail SYMBOL]   format 1 (raw copy)
    census.py STATE.bin BUILD.elf --uncovered                   format 2 (named)

STATE.bin is a state, BUILD.elf the ELF of the build that wrote it. Format 1:
every 4-aligned word of the game's .data and .bss is classified; the words that
look like host pointers (into the ROM image, the binary, the heap) are listed
per global, with the symbol they point at. Format 2: the words of the saved
globals that point into the binary and that no relocation covers
(--uncovered): each one is either a number that looks like an address (a tile
pair, a position) or a pointer that needs a rule in state_pointers_3ds.c.
Standard library only.
"""
import argparse, bisect, os, re, struct, subprocess, sys

NM = os.path.join(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM"), "bin", "arm-none-eabi-nm")


def symbols(elf):
    out = subprocess.run([NM, "-n", "-S", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    syms = []
    marks = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            marks[p[2]] = int(p[0], 16)
        elif len(p) == 4:
            syms.append((int(p[0], 16), int(p[1], 16), p[2], p[3]))
    return syms, marks


def uncovered(a, raw, syms, marks, rom_base):
    """Format 2: pointer-looking words of the scanned globals with no relocation."""
    pos = 68
    def u32():
        nonlocal pos
        v = struct.unpack_from("<I", raw, pos)[0]; pos += 4; return v
    def name():
        nonlocal pos
        n = raw[pos]; pos += 1
        s = raw[pos:pos + n].decode(); pos += n; return s
    n = u32()
    vars_ = []
    for _ in range(n):
        nm = name(); sz = u32(); vars_.append((nm, raw[pos:pos + sz])); pos += sz
    covered = set()
    kinds = {}
    for _ in range(u32()):
        kind = raw[pos]; pos += 1
        holder = name(); off = u32(); tgt = name(); u32()
        covered.add((holder, off)); kinds[kind] = kinds.get(kind, 0) + 1
    by_addr = sorted(syms); addrs = [s[0] for s in by_addr]
    def sym_at(v):
        i = bisect.bisect_right(addrs, v) - 1
        if i >= 0 and by_addr[i][0] <= v < by_addr[i][0] + max(by_addr[i][1], 1):
            return f"{by_addr[i][3]}+{v - by_addr[i][0]:#x}"
        return None
    lo, hi = 0x100000, marks.get("__end__", 0x800000)
    total = 0
    print(f"# {len(vars_)} globals, {len(covered)} relocations {kinds}")
    for nm, blob in vars_:
        if len(blob) > 16384:
            continue
        rows = []
        for off in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, off)[0]
            if lo <= v < hi and (nm, off) not in covered:
                t = sym_at(v)
                if t:
                    rows.append((off, v, t))
        if rows:
            total += len(rows)
            print(f"{nm} (size {len(blob)}): {len(rows)} uncovered")
            for off, v, t in rows[:a.min if a.min > 1 else 6]:
                print(f"    +{off:#x} {v:#010x} {t}")
    print(f"# {total} uncovered words")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state"); ap.add_argument("elf")
    ap.add_argument("--min", type=int, default=1)
    ap.add_argument("--detail")
    ap.add_argument("--uncovered", action="store_true")
    a = ap.parse_args()
    syms, marks = symbols(a.elf)
    raw = open(a.state, "rb").read()
    magic, ver, fp, rom_id, rom_base, dsz, bsz = struct.unpack_from("<8sIIIIII", raw, 0)
    if ver == 2:
        return uncovered(a, raw, syms, marks, rom_base)
    body = raw[68:]
    data, bss = body[:dsz], body[dsz:dsz + bsz]
    d0, b0 = marks["__ss_data_start"], marks["__ss_bss_start"]
    rom_size = 16 * 1024 * 1024
    code_lo, code_hi = marks["__ss_text_start"], marks["__ss_text_end"]
    img_lo, img_hi = 0x100000, marks.get("__end__", 0x700000)
    by_addr = sorted(syms)
    addrs = [s[0] for s in by_addr]

    def sym_at(v):
        i = bisect.bisect_right(addrs, v) - 1
        while i >= 0:
            s = by_addr[i]
            if s[0] <= v < s[0] + max(s[1], 1):
                return f"{s[3]}+{v - s[0]:#x}"
            if v - s[0] > 0x10000:
                break
            i -= 1
        return None

    def cls(v):
        if v == 0 or v < 0x100000:
            return None
        if rom_base <= v < rom_base + rom_size:
            return "ROM"
        if code_lo <= v < code_hi:
            return "CODE"
        if img_lo <= v < img_hi:
            return "IMAGE"
        if 0x08000000 <= v < 0x10000000 and not (0x08000000 <= v < 0x08000000 + rom_size and False):
            return "HEAP"
        if 0x14000000 <= v < 0x20000000:
            return "LINEAR"
        return None

    owners = {}
    for addr, size, typ, name in syms:
        if typ in "dDbB" and size and (d0 <= addr < marks["__ss_data_end"] or b0 <= addr < marks["__ss_bss_end"]):
            owners[addr] = (size, name)
    starts = sorted(owners)

    report = {}
    for base, blob in ((d0, data), (b0, bss)):
        for off in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, off)[0]
            c = cls(v)
            if not c:
                continue
            addr = base + off
            i = bisect.bisect_right(starts, addr) - 1
            if i < 0 or addr >= starts[i] + owners[starts[i]][0]:
                owner = f"?{addr:#x}"; o_off = 0; size = 4
            else:
                owner = owners[starts[i]][1]; o_off = addr - starts[i]; size = owners[starts[i]][0]
            r = report.setdefault(owner, {"size": size, "words": []})
            r["words"].append((o_off, c, v))

    if a.detail:
        r = report.get(a.detail)
        for o, c, v in (r["words"] if r else []):
            print(f"  +{o:#06x} {c:6s} {v:#010x} {sym_at(v) or ''}")
        return
    print(f"# {len(report)} globals hold host-pointer-looking words; ROM base {rom_base:#x}")
    for name, r in sorted(report.items(), key=lambda kv: -len(kv[1]["words"])):
        if len(r["words"]) < a.min:
            continue
        kinds = {}
        for _, c, _ in r["words"]:
            kinds[c] = kinds.get(c, 0) + 1
        ex = r["words"][0]
        print(f"{name:40s} size {r['size']:7d} words {len(r['words']):5d} {kinds}  e.g. +{ex[0]:#x} -> {sym_at(ex[2]) or hex(ex[2])}")


if __name__ == "__main__":
    main()
