#!/usr/bin/env python3
"""Decode the compressed vertex and index streams of Moxie .mxm meshes.

Re-implementation of MOXIE::VertexCompression::decompress (0x1800bd3e0), its attribute
decoder FUN_1800bbcb0 and decompressIndices (0x1800bdc90). Bit order: LSB-first
(MOXIE::BitStream::read). See docs/file-formats.md.

Usage:
    mxm_geometry.py --check <dir>        decode every mesh of every .mxm, validate, summarize
    mxm_geometry.py --obj <file.mxm> <out.obj>   export positions/normals/uvs/triangles
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mxm  # noqa: E402


class Bits:
    def __init__(self, data):
        self.d = data
        self.n = len(data) * 8

    def read(self, pos, width):
        """MOXIE::BitStream::read(pos, width): LSB-first, returns 0 for width <= 0."""
        if width <= 0:
            return 0
        if pos + width > self.n:
            raise EOFError(f"bit read {pos}+{width} beyond {self.n}")
        v = int.from_bytes(self.d[pos >> 3:((pos + width + 7) >> 3)], "little")
        return (v >> (pos & 7)) & ((1 << width) - 1)

    def read_array(self, pos, width, count):
        if width == 0:
            return [0] * count, pos
        return [self.read(pos + i * width, width) for i in range(count)], pos + width * count


def signmag(v, e):
    """Fixed-point sign/magnitude used for ranges: magnitude bits [0,e), sign bit e, unit 1/256."""
    mag = v & ((1 << e) - 1)
    return (-mag if (v >> e) & 1 else mag) * (1.0 / 256.0)


def decode_attribute(bs, pos, ncomp, count):
    """FUN_1800bbcb0 -> (values[count][ncomp], new bit position, info)."""
    mode = bs.read(pos, 4)
    pos += 4
    info = {"mode": mode}
    if mode & 8:  # raw 32-bit floats
        out = []
        for _ in range(count * ncomp):
            out.append(struct.unpack("<f", struct.pack("<I", bs.read(pos, 32)))[0])
            pos += 32
        return [out[i * ncomp:(i + 1) * ncomp] for i in range(count)], pos, info
    widths, pos = bs.read_array(pos, 5, ncomp)
    e = bs.read(pos, 5)
    pos += 5
    info.update(widths=widths, e=e)
    if mode & 4:  # one constant per component
        raw, pos = bs.read_array(pos, e + 1, ncomp)
        consts = [signmag(v, e) for v in raw]
        pos += sum(widths)  # the engine advances by the widths although nothing is read
        return [list(consts) for _ in range(count)], pos, info
    raw, pos = bs.read_array(pos, e + 1, 2 * ncomp)
    mins = [signmag(v, e) for v in raw[:ncomp]]
    maxs = [signmag(v, e) for v in raw[ncomp:]]
    info.update(mins=mins, maxs=maxs)
    shared_idx = None
    per_idx = [None] * ncomp
    shared_size = per_size = None
    if mode & 1:  # one index array shared by all components
        iw = bs.read(pos, 5)
        shared_size = bs.read(pos + 5, iw) + 1
        pos += 5 + iw
        shared_idx, pos = bs.read_array(pos, iw, count)
        info["palette"] = shared_size
    elif mode & 2:  # an index array per component (only for components with width != 0)
        iws, pos = bs.read_array(pos, 5, ncomp)
        per_size = [0] * ncomp
        for c in range(ncomp):
            if widths[c]:
                iw = iws[c]
                per_size[c] = bs.read(pos, iw) + 1
                per_idx[c], _ = bs.read_array(pos + iw, iw, count)
                pos += iw + iw * count
        info["palettes"] = per_size
    cols = []
    for c in range(ncomp):
        w = widths[c]
        n = count
        idx = None
        if w:
            if mode & 1:
                n, idx = shared_size, shared_idx
            elif mode & 2:
                n, idx = per_size[c], per_idx[c]
        scale = (maxs[c] - mins[c]) * (1.0 / float(1 << w))
        vals = []
        for _ in range(n):
            q = bs.read(pos, w)
            pos += w
            vals.append(q * scale + mins[c])
        cols.append([vals[j] for j in idx] if idx is not None else vals)
    return [[cols[c][i] for c in range(ncomp)] for i in range(count)], pos, info


def decode_vertices(blob, instances=1):
    """VertexCompression::decompress -> dict of attribute arrays."""
    bs = Bits(blob)
    count = bs.read(0, 24)
    if count == 0:
        return {"count": 0}, 24
    flags = bs.read(24, 16)
    pos = 40
    out = {"count": count, "flags": flags}
    for bit, name, ncomp in ((0, "position", 3), (1, "normal", 3), (2, "tangent", 3), (7, "color", 4)):
        if flags & (1 << bit):
            out[name], pos, out[name + "_info"] = decode_attribute(bs, pos, ncomp, count)
    for k in range(4):
        if flags & (8 << k):
            out[f"uv{k}"], pos, out[f"uv{k}_info"] = decode_attribute(bs, pos, 2, count)
    if flags & 0x100:
        w, pos, out["weights_info"] = decode_attribute(bs, pos, 4, count)
        out["weights"] = [[x / max(sum(v), 1.1920928955078125e-07) for x in v] for v in w]
    if flags & 0x200:
        base = bs.read(pos, 8)
        width = bs.read(pos + 8, 4)
        pos += 12
        joints = []
        for _ in range(count):
            vals, pos = bs.read_array(pos, width, 4)
            joints.append([v + base for v in vals])
        out["joints"] = joints
    return out, pos


def decode_indices(blob):
    bs = Bits(blob)
    n = bs.read(0, 32)
    if n == 0:
        return [], 32
    mode = bs.read(32, 2)
    if mode == 0:
        width = bs.read(34, 4) + 1
        base = bs.read(38, width)
        vals, pos = bs.read_array(38 + width, width, n)
        return [(v + base) & 0xFFFF for v in vals], pos
    if mode == 1:
        vals, pos = bs.read_array(34, 16, n)
        return vals, pos
    raise ValueError(f"index mode {mode}")


def mesh_geometry(data, mesh):
    vo, vs = mesh["vertex_blob"]
    io, isz = mesh["index_blob"]
    verts, vbits = decode_vertices(data[vo:vo + vs])
    idx, ibits = decode_indices(data[io:io + isz])
    return verts, idx, (vbits, vs * 8), (ibits, isz * 8)


def check(root):
    stats = {"files": 0, "meshes": 0, "empty": 0, "bad_bits": 0, "bad_idx": 0, "not_tri": 0,
             "normal_dev": 0.0, "verts": 0, "tris": 0, "errors": 0}
    modes = {}
    examples = []
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if not fn.endswith(".mxm"):
                continue
            p = os.path.join(dp, fn)
            data = open(p, "rb").read()
            m = mxm.parse(data)
            stats["files"] += 1
            for me in m.get("meshes", []):
                stats["meshes"] += 1
                try:
                    v, idx, (vb, vmax), (ib, imax) = mesh_geometry(data, me)
                except Exception as e:  # noqa: BLE001
                    stats["errors"] += 1
                    if len(examples) < 5:
                        examples.append((fn, f"{type(e).__name__}: {e}"))
                    continue
                if v["count"] == 0:
                    stats["empty"] += 1
                    continue
                stats["verts"] += v["count"]
                stats["tris"] += len(idx) // 3
                if not (vmax - 8 < vb <= vmax) or not (imax - 8 < ib <= imax):
                    stats["bad_bits"] += 1
                    if len(examples) < 5:
                        examples.append((fn, f"bits v {vb}/{vmax} i {ib}/{imax}"))
                if idx and max(idx) >= v["count"]:
                    stats["bad_idx"] += 1
                if len(idx) % 3:
                    stats["not_tri"] += 1
                for k in ("position", "normal", "tangent", "color", "uv0"):
                    if k + "_info" in v:
                        modes.setdefault(k, {}).setdefault(v[k + "_info"]["mode"], 0)
                        modes[k][v[k + "_info"]["mode"]] += 1
                if "normal" in v:
                    dev = max(abs(math.sqrt(sum(c * c for c in n)) - 1.0) for n in v["normal"])
                    stats["normal_dev"] = max(stats["normal_dev"], dev)
    print(stats)
    print("attribute modes:", modes)
    for ex in examples:
        print("  e.g.", ex)
    return 0


def export_obj(path, out):
    data = open(path, "rb").read()
    m = mxm.parse(data)
    if "base_model" in m:
        raise SystemExit(f"patch model; geometry lives in {m['base_model']}")
    lines = [f"# {os.path.basename(path)}"]
    base = 1
    for i, me in enumerate(m["meshes"]):
        v, idx, _, _ = mesh_geometry(data, me)
        if not v.get("count"):
            continue
        lines.append(f"o mesh{i}")
        for p in v["position"]:
            lines.append("v %.6f %.6f %.6f" % tuple(p))
        for t in v.get("uv0", []):
            lines.append("vt %.6f %.6f" % tuple(t))
        for n in v.get("normal", []):
            lines.append("vn %.6f %.6f %.6f" % tuple(n))
        for a in range(0, len(idx) - 2, 3):
            f = [idx[a] + base, idx[a + 1] + base, idx[a + 2] + base]
            lines.append("f " + " ".join(f"{x}/{x}/{x}" for x in f))
        base += v["count"]
    open(out, "w").write("\n".join(lines) + "\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    if sys.argv[1] == "--check":
        sys.exit(check(sys.argv[2]))
    if sys.argv[1] == "--obj":
        export_obj(sys.argv[2], sys.argv[3])
