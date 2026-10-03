#!/usr/bin/env python3
"""Find all luaL_Reg tables ({const char* name, lua_CFunction func} ..., {NULL, NULL}) in a PE
image: the complete list of native functions a Moxie DLL exposes to Lua, with the address of
every implementation (for Ghidra).

Usage: lua_reg_scan.py <dll> [--min N] > tables.txt
Output: one block per table: table VA, then 'name  funcVA'.
"""
import re
import struct
import sys

import pefile

IDENT = re.compile(rb"^[A-Za-z_][A-Za-z0-9_]*$")


def main(argv):
    path = argv[1]
    min_entries = int(argv[argv.index("--min") + 1]) if "--min" in argv else 2
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    secs = [(s.Name.rstrip(b"\0").decode(), base + s.VirtualAddress, s.Misc_VirtualSize, s) for s in pe.sections]
    text = [(a, n) for name, a, n, _ in secs if name == ".text"]

    def in_text(va):
        return any(a <= va < a + n for a, n in text)

    def cstr(va):
        for name, a, n, s in secs:
            if a <= va < a + n:
                off = va - a
                data = s.get_data()
                end = data.find(b"\0", off)
                if end < 0 or end - off > 64:
                    return None
                return data[off:end]
        return None

    tables = []
    for name, a, n, s in secs:
        if name not in (".rdata", ".data"):
            continue
        data = s.get_data()
        i = 0
        while i + 16 <= len(data):
            entries = []
            j = i
            while j + 16 <= len(data):
                p_name, p_func = struct.unpack_from("<QQ", data, j)
                if p_name == 0 and p_func == 0:
                    break
                nm = cstr(p_name) if p_name else None
                if not nm or not IDENT.match(nm) or not in_text(p_func):
                    entries = None
                    break
                entries.append((nm.decode(), p_func))
                j += 16
            if entries and len(entries) >= min_entries and j + 16 <= len(data):
                tables.append((a + i, entries))
                i = j + 16
            else:
                i += 8
    for va, entries in tables:
        print(f"## table {va:#x} ({len(entries)})")
        for nm, f in entries:
            print(f"  {nm:32s} {f:#x}")
    print(f"# {len(tables)} tables, {sum(len(e) for _, e in tables)} functions", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv)
