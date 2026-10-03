#!/usr/bin/env python3
"""Moxie .shd shader permutation parser.

Layout (empirical, verified on all shipped .shd files with --check):
    u32 0x00010001 · Uri master (.msh) · u32 nPrograms(=2) · 8 bytes ·
    u32 len · VS text · u32 len · FS text ·
    u32 flags · u32 hash · u32 nParams · u32 nSemantics ·
    Uniform[nParams + nSemantics]
Uniform: u16 class (0 int, 1 float, 2 matrix, 3 sampler, 4 sampler (other type)) · u16 type · u16 semantic
         (0xFFFF = material parameter, else engine-supplied value id) · u16 count ·
         u16 size · u16 size2 · u32 len · name[len] · (class 3/4 samplers: u32 texture unit)

Usage: shd.py <file.shd> [--src] | --check <dir> | --semantics <dir>
"""
import collections
import os
import struct
import sys


def parse(d):
    if struct.unpack_from("<I", d, 0)[0] != 0x10001:
        raise ValueError("bad magic")
    o = 5
    _h, size, _a, _b = struct.unpack_from("<4I", d, o)
    o += 16
    master = d[o:o + size].split(b"\0")[2].decode("latin-1")
    o += size + 12
    nprog = struct.unpack_from("<I", d, o)[0]
    o += 4 + 8
    progs = []
    for _ in range(nprog):
        n = struct.unpack_from("<I", d, o)[0]
        progs.append(d[o + 4:o + 4 + n].decode("latin-1"))
        o += 4 + n
    flags, h, npar, nsem = struct.unpack_from("<4I", d, o)
    o += 16
    uniforms = []
    for i in range(npar + nsem):
        cls, typ, sem, cnt, s1, s2, n = struct.unpack_from("<6HI", d, o)
        o += 16
        name = d[o:o + n].decode("latin-1")
        o += n
        unit = None
        if cls >= 3:
            unit = struct.unpack_from("<I", d, o)[0]
            o += 4
        uniforms.append({"name": name, "class": cls, "type": typ, "semantic": sem,
                         "count": cnt, "size": s1, "size2": s2, "unit": unit})
    return {"master": master, "vs": progs[0], "fs": progs[1], "flags": flags, "hash": h,
            "nparams": npar, "uniforms": uniforms, "end": o}


def walk(root):
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if fn.endswith(".shd"):
                yield os.path.join(dp, fn)


def main(argv):
    if argv[1] == "--check":
        ok = bad = 0
        for p in walk(argv[2]):
            d = open(p, "rb").read()
            try:
                s = parse(d)
                if s["end"] != len(d):
                    raise ValueError(f"end {s['end']} != {len(d)}")
                ok += 1
            except Exception as e:  # noqa: BLE001
                bad += 1
                print(f"{p}: ERROR {e}")
        print(f"ok {ok} bad {bad}")
        return 0
    if argv[1] == "--semantics":
        sem = collections.defaultdict(collections.Counter)
        for p in walk(argv[2]):
            try:
                us = parse(open(p, "rb").read())["uniforms"]
            except Exception:  # noqa: BLE001
                continue
            for u in us:
                if u["semantic"] != 0xFFFF:
                    sem[u["semantic"]][(u["name"], u["class"], u["type"], u["count"])] += 1
        for k in sorted(sem):
            print(f"{k:3d} 0x{k:02x}: " + ", ".join(f"{n} c{c} t{t} x{cnt} ({m})" for (n, c, t, cnt), m in sem[k].items()))
        return 0
    s = parse(open(argv[1], "rb").read())
    print(f"master {s['master']} flags 0x{s['flags']:x} hash 0x{s['hash']:08x} params {s['nparams']}")
    for u in s["uniforms"]:
        print(f"  {u['name']:28s} class {u['class']} type {u['type']} sem {u['semantic']:#06x} "
              f"count {u['count']} size {u['size']} unit {u['unit']}")
    if "--src" in argv:
        print(s["vs"])
        print("// ---- fragment")
        print(s["fs"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
