#!/usr/bin/env python3
"""Fill the save state symbol table of a linked 3DS ELF (PLAN P1.8b).

A save state stores the game's mutable globals by NAME and its pointers as
relocations, so it loads into a build whose globals and functions have moved.
The names only exist in the ELF's symbol table, so the table is built from the
linked ELF and patched into it:

  * platform/3ds/source/state_blob_3ds.c reserves a zero-filled array
    (gStateSymbolBlob, STATE_SYMBOL_BLOB_SIZE bytes);
  * after the link (CMake POST_BUILD), this script reads the symbols and
    overwrites that array in the ELF file. Nothing moves: the array keeps its
    size and address, and the CIA is made from the patched ELF.

What goes in (little endian):

    u32 magic 'TST1', u32 nVars, u32 nSyms, u32 nameBytes
    nVars x { u32 nameOffset, u32 regionAndOffset, u32 size }
        the game's .data / .bss objects (3dsx.ld brackets __ss_data_* and
        __ss_bss_*); bit 31 of regionAndOffset set = .bss; the rest is the byte
        offset from the start of that bracket
    nSyms x { u32 nameHash, u32 address, u32 size }, sorted by address
        every function of the game's code and every object of its constants:
        what a saved pointer can name when it is not one of the vars
    nameBytes of NUL-terminated var names

A name is the symbol with a trailing `.<digits>` removed (the compiler's suffix
on function-local statics, which changes between builds); a repeat gets `#<n>`.
The hash is FNV-1a 32 over that name; a collision among the symbols fails the
build rather than corrupting a state.

Usage:  python3 tools/gen_state_symbols.py <elf> [--nm PATH] [--readelf PATH]
"""
import argparse
import os
import re
import struct
import subprocess
import sys

SYMBOL = "gStateSymbolBlob"
MAGIC = 0x31545354  # 'TST1'


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def fnv1a(name):
    h = 2166136261
    for b in name.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    base = os.path.join(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM"), "bin", "arm-none-eabi-")
    ap.add_argument("--nm", default=base + "nm")
    ap.add_argument("--readelf", default=base + "readelf")
    args = ap.parse_args()

    syms = []
    marks = {}
    for line in run([args.nm, "-n", "-S", "--defined-only", args.elf]).splitlines():
        p = line.split()
        if len(p) == 3:
            addr, typ, name = int(p[0], 16), p[1], p[2]
            size = 0
        elif len(p) == 4:
            addr, size, typ, name = int(p[0], 16), int(p[1], 16), p[2], p[3]
        else:
            continue
        if name.startswith("__ss_"):
            marks[name] = addr
        else:
            syms.append((addr, size, typ, name))
    for need in ("__ss_text_start", "__ss_text_end", "__ss_rodata_start", "__ss_rodata_end",
                 "__ss_data_start", "__ss_data_end", "__ss_bss_start", "__ss_bss_end"):
        if need not in marks:
            sys.exit(f"gen_state_symbols: {need} not found in {args.elf}")

    def unique_names(items):
        seen = {}
        out = []
        for addr, size, typ, name in items:
            key = re.sub(r"\.\d+$", "", name)
            n = seen.get(key, 0)
            seen[key] = n + 1
            out.append((addr, size, typ, key if n == 0 else f"{key}#{n}"))
        return out

    var_items, code_items = [], []
    for addr, size, typ, name in syms:
        if typ in "dDbB" and size and (marks["__ss_data_start"] <= addr < marks["__ss_data_end"]
                                      or marks["__ss_bss_start"] <= addr < marks["__ss_bss_end"]):
            var_items.append((addr, size, typ, name))
        elif marks["__ss_text_start"] <= addr < marks["__ss_text_end"] or \
                (typ in "rRtTwW" and marks["__ss_rodata_start"] <= addr < marks["__ss_rodata_end"]):
            if name.startswith("$") or name.startswith(".L") or name.startswith("__"):
                continue
            code_items.append((addr, size, typ, name))
    var_items = unique_names(var_items)
    code_items = unique_names(code_items)

    names = b""
    vtable = b""
    for addr, size, typ, name in var_items:
        if marks["__ss_data_start"] <= addr < marks["__ss_data_end"]:
            region, off = 0, addr - marks["__ss_data_start"]
        else:
            region, off = 1, addr - marks["__ss_bss_start"]
        vtable += struct.pack("<III", len(names), (region << 31) | off, size)
        names += name.encode() + b"\0"

    by_hash = {}
    stable = b""
    # Several symbols at one address (aliases): keep the first name only.
    seen_addr = set()
    for addr, size, typ, name in sorted(code_items):
        if addr in seen_addr:
            continue
        seen_addr.add(addr)
        h = fnv1a(name)
        if h in by_hash and by_hash[h] != name:
            sys.exit(f"gen_state_symbols: hash collision between {name} and {by_hash[h]}")
        by_hash[h] = name
        stable += struct.pack("<III", h, addr, size)
    blob = struct.pack("<IIII", MAGIC, len(var_items), len(stable) // 12, len(names)) + vtable + stable + names

    sym = None
    for line in run([args.nm, "-n", "-S", "--defined-only", args.elf]).splitlines():
        p = line.split()
        if len(p) == 4 and p[3] == SYMBOL:
            sym = (int(p[0], 16), int(p[1], 16))
    if sym is None:
        sys.exit(f"gen_state_symbols: no {SYMBOL} in {args.elf} (is state_blob_3ds.c linked?)")
    addr, cap = sym
    off = None
    for line in run([args.readelf, "-S", "-W", args.elf]).splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if m:
            sec_addr, sec_off, sec_size = int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
            if sec_addr != 0 and sec_addr <= addr and addr + cap <= sec_addr + sec_size:  # not .debug_*, which sit at 0
                off = sec_off + (addr - sec_addr)
    if off is None:
        sys.exit(f"gen_state_symbols: {SYMBOL} is not inside a PROGBITS section of {args.elf}")
    if len(blob) > cap:
        sys.exit(f"gen_state_symbols: table needs {len(blob)} bytes, {SYMBOL} holds {cap}: raise STATE_SYMBOL_BLOB_SIZE")
    used = len(blob)
    blob += b"\0" * (cap - len(blob))

    with open(args.elf, "r+b") as f:
        f.seek(off)
        f.write(blob)
    print(f"gen_state_symbols: {len(var_items)} variables, {len(stable) // 12} functions/constants, "
          f"{used} of {cap} bytes")


if __name__ == "__main__":
    main()
