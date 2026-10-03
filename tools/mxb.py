#!/usr/bin/env python3
"""Moxie .mxb particle system parser — derived from ParticleSystemResource::onLoad
(0x180149fa0), ParticleDefinition::read (0x180124cf0), ParticleField/ForceField/
TurbulenceField3D/TurbulenceField2D/BobberField::read and PlaneCollider::read. Element
counts dropped by Ghidra were recovered with tools/ghidra/read_layout.py.

Usage: mxb.py <file.mxb> | --check <dir>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mxm  # noqa: E402

TYPE_PARTICLES = 0x88F89426
# ParticleDefinition fixed part: (struct offset, element format, count) in stream order
DEF_LAYOUT = [
    (0x00, "I", 2), (0x08, "f", 1), (0x0C, "f", 1), (0x10, "f", 1), (0x14, "f", 1), (0x18, "f", 1),
    (0x1C, "B", 1), (0x20, "f", 1), (0x24, "f", 3), (0x30, "B", 1), (0x34, "f", 16), (0x74, "I", 1),
    (0x78, "f", 4), (0x88, "f", 3), (0x94, "B", 1), (0x98, "f", 1), (0x9C, "B", 1), (0xA0, "f", 1),
    (0xA4, "B", 1), (0xA8, "f", 8), (0xC8, "I", 1), (0xCC, "f", 4), (0xDC, "f", 3), (0xE8, "B", 1),
    (0xEC, "f", 1), (0xF0, "B", 1), (0xF4, "f", 1), (0xF8, "B", 1), (0xFC, "f", 4), (0x10C, "I", 1),
    (0x110, "f", 4), (0x120, "f", 3), (0x12C, "B", 1), (0x130, "f", 1), (0x134, "B", 1),
    (0x138, "f", 1), (0x13C, "B", 1), (0x140, "f", 2), (0x148, "f", 8), (0x168, "f", 4),
    (0x178, "f", 2), (0x180, "f", 6), (0x198, "f", 2), (0x1A0, "f", 2), (0x1A8, "f", 2),
    (0x1B0, "f", 6), (0x1C8, "f", 1), (0x1CC, "f", 1), (0x1D0, "f", 2), (0x1D8, "f", 1),
    (0x1DC, "f", 1), (0x1E0, "f", 2), (0x1E8, "B", 1), (0x1E9, "B", 1), (0x1EA, "B", 1),
    (0x1EB, "B", 1), (0x1EC, "B", 1), (0x1F0, "f", 1), (0x1F4, "f", 1), (0x1F8, "f", 1),
    (0x1FC, "f", 1), (0x200, "f", 1), (0x204, "f", 1),
]
FIELD_LAYOUT = {  # after the 32-byte name read by ParticleField::read
    0: ("ForceField", [("f", 3), ("B", 1), ("f", 16)]),
    1: ("TurbulenceField3D", [("f", 8)]),
    2: ("TurbulenceField2D", [("f", 5)]),
    3: ("BobberField", [("f", 3), ("f", 3)]),
}
COLLIDER_LAYOUT = {0: ("PlaneCollider", [("f", 3), ("f", 1), ("f", 1), ("f", 1), ("f", 1), ("f", 3)])}


def take_fmt(r, fmt, n):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", r.take(size * n))


def read_definition(r):
    d = {}
    for off, fmt, n in DEF_LAYOUT:
        v = take_fmt(r, fmt, n)
        d[off] = v[0] if n == 1 else v
    version = d[0x204]
    if version >= 1.9999:
        d["name256"] = r.take(256).split(b"\0")[0].decode("latin-1")
        d[0x278], d[0x27C] = take_fmt(r, "I", 2)
    if version >= 2.0999:
        d[0x280] = r.u8()
    d["version"] = version
    return d


def parse(data):
    r = mxm.Reader(data)
    magic, tid, version = r.u32(), r.u32(), r.u32()
    if magic != mxm.MAGIC or tid != TYPE_PARTICLES:
        raise ValueError("not a particle system")
    ps = {"version": version, "name": r.string(), "material": mxm.read_material(r)}
    ps["definition"] = read_definition(r)
    nf, nc = r.u16(), r.u16()
    ps["fields"] = []
    for _ in range(nf):
        t = r.u32()
        name = r.take(32).split(b"\0")[0].decode("latin-1")
        cls, lay = FIELD_LAYOUT[t]
        ps["fields"].append((cls, name, [take_fmt(r, f, n) for f, n in lay]))
    ps["colliders"] = []
    for _ in range(nc):
        t = r.u32()
        name = r.take(32).split(b"\0")[0].decode("latin-1")
        cls, lay = COLLIDER_LAYOUT[t]
        ps["colliders"].append((cls, name, [take_fmt(r, f, n) for f, n in lay]))
    if version == 2:
        a, b = r.u32(), r.u32()
        ps["map"] = (a, b)
        if a and b:
            ps["map_data"] = r.take(a)
    ps["end"] = r.p
    return ps


def main(argv):
    if argv[1] == "--check":
        for dp, _, fns in os.walk(argv[2]):
            for fn in fns:
                if fn.endswith(".mxb"):
                    d = open(os.path.join(dp, fn), "rb").read()
                    try:
                        ps = parse(d)
                        print(f"{fn}: end {ps['end']}/{len(d)} def v{ps['definition']['version']:.2f} "
                              f"fields {[f[0] for f in ps['fields']]} colliders {len(ps['colliders'])} "
                              f"map {ps.get('map')}")
                    except Exception as e:  # noqa: BLE001
                        print(f"{fn}: ERROR {type(e).__name__}: {e}")
        return 0
    ps = parse(open(argv[1], "rb").read())
    print({k: v for k, v in ps.items() if k not in ("material", "definition", "map_data")})
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
