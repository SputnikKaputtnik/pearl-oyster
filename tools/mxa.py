#!/usr/bin/env python3
"""Moxie .mxa (AnimResource) parser — derived from MOXIE::AnimResource::read (0x180140c40),
readTrack (0x180141190), readChannel (0x1801415a0), readCustomChannel (0x180141820).

Usage:
    mxa.py <file.mxa>          summary
    mxa.py --check <dir>       parse all .mxa, report files not ending exactly at EOF
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mxm import MAGIC, Reader  # noqa: E402

TYPE_ANIM = 0x42A975E4
VALUE_SIZE = {0: 4, 1: 4, 2: 4, 3: 8, 4: 12, 5: 16, 6: 16}


def read_channel(r):
    ch = {"attr": r.u32(), "vtype": r.u32()}
    n = r.u32()
    ch["keys"] = [struct.unpack("<3I", r.take(12)) for _ in range(n)]  # (frame?, blob offset, ?)
    return ch


def read_track(r):
    t = {"name": r.string()}
    n = r.u32()
    t["blob"] = r.take(n)
    nch, ncust = r.u32(), r.u32()
    t["channels"] = [read_channel(r) for _ in range(nch)]
    t["custom"] = []
    for _ in range(ncust):
        a = r.u32()
        k = r.u32()
        vals = struct.unpack(f"<{k}I", r.take(4 * k))
        c = read_channel(r)
        c.update(custom_a=a, custom_vals=vals)
        t["custom"].append(c)
    return t


def parse(data, stop_before_vertex_anim=True):
    r = Reader(data)
    magic, tid, version = r.u32(), r.u32(), r.u32()
    if magic != MAGIC or tid != TYPE_ANIM or version not in (1, 2):
        raise ValueError("not a supported Moxie animation")
    a = {"version": version, "frames": r.u32(), "fps": r.u32()}
    a["tracks"] = [read_track(r) for _ in range(r.u32())]
    a["u32_50"] = r.u32()
    a["has_vertex_anim"] = r.u8()
    a["vertex_anim_at"] = r.p
    if a["has_vertex_anim"] and version == 2:
        a["gsa_id"] = r.u32()
    elif a["has_vertex_anim"]:
        a["vertex_anim"] = read_vertex_anim(r)
    a["end"] = r.p
    return a


def read_bitblock(r):
    nbits = r.u32()
    return nbits, r.take((nbits + 7) >> 3)


def read_vertex_anim(r):
    """FUN_1800a9b20 + per mesh FUN_1800a9c50(.., withBounds=1) + chunks FUN_1800aa1d0.
    Two reads had their element count dropped by Ghidra (4-byte magic, 8-byte mesh field)."""
    magic = r.take(4)
    if magic != b"VANM":
        raise ValueError(f"vertex anim magic {magic!r}")
    va = {"version": r.u32(), "u32": r.u32(), "fps": r.f32()}
    meshes = []
    for _ in range(r.u32()):
        m = {"name": r.string(), "u32_70": r.u32()}
        nchunks = r.u32()
        m["frames"] = r.u32()
        m["field8"] = r.take(8)
        nblk = r.u32()
        m["visibility"] = struct.unpack(f"<{nblk}H", r.take(2 * nblk))  # 1 bit per frame, 16 per block
        m["bounds"] = [struct.unpack("<6f", r.take(24)) for _ in range(nblk)]  # min xyz, max xyz
        chunks = []
        for _ in range(nchunks):
            c = {"a": r.u32(), "frames": r.u32(), "c": r.u32()}
            c["blocks"] = [read_bitblock(r) for _ in range((c["frames"] + 15) >> 4)]
            c["header"] = read_bitblock(r)
            chunks.append(c)
        m["chunks"] = chunks
        meshes.append(m)
    va["meshes"] = meshes
    return va


def check(root):
    res = collections.Counter()
    ex = {}
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if not fn.endswith(".mxa"):
                continue
            data = open(os.path.join(dp, fn), "rb").read()
            try:
                a = parse(data)
                key = (a["version"], a["has_vertex_anim"], "EOF" if a["end"] == len(data) else "rest")
                res[key] += 1
                ex.setdefault(key, (fn, len(data) - a["end"], a.get("gsa_id")))
            except Exception as e:  # noqa: BLE001
                res[("error", type(e).__name__)] += 1
                ex.setdefault(("error", type(e).__name__), (fn, str(e)))
    for k, n in sorted(res.items(), key=str):
        print(k, n, "e.g.", ex[k])


def summary(path):
    data = open(path, "rb").read()
    a = parse(data)
    print(f"{path}: v{a['version']} frames={a['frames']} fps={a['fps']} tracks={len(a['tracks'])} "
          f"vertexAnim={a['has_vertex_anim']} u32_50={a['u32_50']} end={a['end']:#x}/{len(data):#x}")
    for t in a["tracks"][:6]:
        print(f"  track {t['name']!r} blob={len(t['blob'])}B channels={len(t['channels'])} custom={len(t['custom'])}")
        for c in t["channels"][:4]:
            print(f"     attr={c['attr']} vtype={c['vtype']} keys={len(c['keys'])} first={c['keys'][:3]}")


if __name__ == "__main__":
    if sys.argv[1] == "--check":
        check(sys.argv[2])
    else:
        summary(sys.argv[1])
