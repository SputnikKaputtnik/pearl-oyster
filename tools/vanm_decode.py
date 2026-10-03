#!/usr/bin/env python3
"""Decode Moxie VANM vertex animation (the per-mesh compressed channels inside .mxa v1).

Re-implements VertexAnimQuantizer::decompressChunkHeader (0x1800acb20),
VertexAnimDecoder::decodeFrame (0x1800aa650 / 0x1800aa8c0), decodeComponentArray (0x1800aa950)
and the frame/interpolation logic of VertexAnimator::update (0x1800a7e10).

Per chunk: one header bitstream per 16-frame block + one data bitstream for the whole chunk.
For frame f (index i inside its block) and axis c:
    value = q * (max[i][c] - min[i][c]) / 2^w[i][c] + min[i][c] + off[i][c]
with q read from the data stream at the bit offset stored in the block header; the first frame
of a block is absolute, every following frame is added to the previous frame's value.

Usage:
    vanm_decode.py --check <dir>     decode every frame of every vertex-animated mesh and verify
                                     decoded positions against the stored per-block bounds
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mxa  # noqa: E402
from mxm_geometry import Bits  # noqa: E402


def signmag_top(v, w):
    """sign bit = top bit (w-1), magnitude below, unit 1/256 (DAT_180252cf8 / DAT_180253160)."""
    if w == 0:
        return 0.0
    sign = (v >> (w - 1)) & 1
    mag = v & ((1 << (w - 1)) - 1)
    return (-mag if sign else mag) * (1.0 / 256.0)


def parse_block_header(blob, nframes):
    bs = Bits(blob)
    b0 = blob[0] if blob else 0
    wa, wb = b0 >> 3, b0 & 7
    w3, pos = bs.read_array(8, wb, 3)
    w9, pos = bs.read_array(pos, wb, 9)
    qw, pos = bs.read_array(pos, wb, 3 * nframes)          # per frame, per axis: quant width
    offsets, pos = bs.read_array(pos, wa, nframes)         # per frame: bit offset in data stream
    base = []
    for g in range(3):                                      # A (min), B (max), C (offset) frame 0
        raw, pos = bs.read_array(pos, w3[g], 3)
        base.append([signmag_top(v, w3[g]) for v in raw])
    seq = []
    for k in range(9):
        raw, pos = bs.read_array(pos, w9[k], nframes - 1)
        seq.append([signmag_top(v, w9[k]) for v in raw])
    mins = [base[0]] + [[seq[0][i], seq[1][i], seq[2][i]] for i in range(nframes - 1)]
    maxs = [base[1]] + [[seq[3][i], seq[4][i], seq[5][i]] for i in range(nframes - 1)]
    offs = [base[2]] + [[seq[6][i], seq[7][i], seq[8][i]] for i in range(nframes - 1)]
    widths = [qw[3 * i:3 * i + 3] for i in range(nframes)]
    return {"offsets": offsets, "widths": widths, "min": mins, "max": maxs, "off": offs,
            "header_bits": pos, "wa": wa, "wb": wb}


def decode_chunk(chunk):
    """-> list over frames of list over vertices of [x, y, z]."""
    nverts = chunk["c"]
    frames = chunk["frames"]
    nbits, data = chunk["header"]          # (named 'header' in mxa.py; it is the data stream)
    bs = Bits(data)
    out = []
    max_bit = 0
    for b, (hbits, hblob) in enumerate(chunk["blocks"]):
        n = min(16, frames - 16 * b)
        h = parse_block_header(hblob, n)
        prev = None
        for i in range(n):
            cur = [[0.0, 0.0, 0.0] for _ in range(nverts)]
            pos = h["offsets"][i]
            for c in range(3):
                w = h["widths"][i][c]
                mn, mx, off = h["min"][i][c], h["max"][i][c], h["off"][i][c]
                scale = (mx - mn) * (1.0 / float(1 << w))
                q, pos = bs.read_array(pos, w, nverts)
                for v in range(nverts):
                    val = q[v] * scale + mn + off
                    cur[v][c] = val + (prev[v][c] if prev is not None else 0.0)
            max_bit = max(max_bit, pos)
            out.append(cur)
            prev = cur
    return out, max_bit, nbits


def sample(frames, f, stepped, loop=False):
    """VertexAnimator::update: frame position f -> stepped copy or linear blend of floor/ceil."""
    f0 = int(f)
    t = f - f0
    f1 = f0 + 1
    if f1 >= len(frames):
        f1 = 0 if loop else len(frames) - 1
    a = frames[min(f0, len(frames) - 1)]
    if stepped:
        return a
    b = frames[f1]
    return [[(b[v][c] - a[v][c]) * t + a[v][c] for c in range(3)] for v in range(len(a))]


def check(root, limit=None):
    files = meshes = frames_total = 0
    worst = 0.0
    bit_issues = 0
    worst_case = None
    for fn in sorted(os.listdir(root)):
        if not fn.endswith(".mxa"):
            continue
        a = mxa.parse(open(os.path.join(root, fn), "rb").read())
        va = a.get("vertex_anim")
        if not va:
            continue
        files += 1
        for m in va["meshes"]:
            pos_chunk = m["chunks"][0]
            if pos_chunk["c"] == 0:
                continue
            frames, used, avail = decode_chunk(pos_chunk)
            meshes += 1
            frames_total += len(frames)
            if used > avail:
                bit_issues += 1
            for f, verts in enumerate(frames):
                if not (m["visibility"][f >> 4] >> (f & 15)) & 1:
                    continue
                bmin, bmax = m["bounds"][f >> 4][:3], m["bounds"][f >> 4][3:]
                for v in verts:
                    for c in range(3):
                        d = max(bmin[c] - v[c], v[c] - bmax[c], 0.0)
                        if d > worst:
                            worst, worst_case = d, (fn, m["name"], f)
        if limit and files >= limit:
            break
    print(f"{files} files, {meshes} meshes, {frames_total} frames decoded; data-stream overruns: "
          f"{bit_issues}; worst distance outside stored block bounds: {worst:.5f} at {worst_case}")


if __name__ == "__main__":
    if sys.argv[1] == "--check":
        check(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None)
