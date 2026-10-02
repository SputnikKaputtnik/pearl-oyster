#!/usr/bin/env python3
"""Survey file headers across an installation, grouped by extension.

Usage: format_probe.py <root> [ext ...]
For each extension: count, size range, distinct leading-u32 magics, and for Moxie
containers (magic 0x0D00D135) the type-id/version fields. DDS files get their
FourCC / pixel format, OGG their codec header. Read-only.
"""
import collections
import os
import struct
import sys

MOXIE_MAGIC = 0x0D00D135


def dds_info(b):
    if b[:4] != b"DDS ":
        return "not-DDS"
    h, w = struct.unpack_from("<II", b, 12)
    mips = struct.unpack_from("<I", b, 28)[0]
    pf_flags, fourcc = struct.unpack_from("<I4s", b, 80)
    caps2 = struct.unpack_from("<I", b, 112)[0]
    fmt = fourcc.decode("latin-1") if pf_flags & 4 else f"rgb{struct.unpack_from('<I', b, 88)[0]}bpp"
    if fmt == "DX10":
        fmt += f"/dxgi{struct.unpack_from('<I', b, 128)[0]}"
    cube = "cube" if caps2 & 0x200 else ""
    return f"{fmt} mips={mips} {cube}".strip(), (w, h)


def main(argv):
    root = argv[1]
    want = {e.lower().lstrip(".") for e in argv[2:]}
    groups = collections.defaultdict(list)
    for dp, _, fns in os.walk(root):
        for fn in fns:
            ext = fn.rsplit(".", 1)[-1].lower() if "." in fn else "(none)"
            if want and ext not in want:
                continue
            p = os.path.join(dp, fn)
            with open(p, "rb") as f:
                b = f.read(160)
            groups[ext].append((os.path.relpath(p, root).replace(os.sep, "/"), os.path.getsize(p), b))
    for ext in sorted(groups):
        items = groups[ext]
        sizes = [s for _, s, _ in items]
        print(f"## .{ext}: {len(items)} files, {min(sizes)}..{max(sizes)} bytes, total {sum(sizes)}")
        magics = collections.Counter()
        detail = collections.Counter()
        dims = collections.Counter()
        for rel, size, b in items:
            if len(b) < 4:
                magics["(short)"] += 1
                continue
            m = struct.unpack_from("<I", b)[0]
            magics[f"0x{m:08X} {b[:4]!r}"] += 1
            if m == MOXIE_MAGIC and len(b) >= 12:
                tid, ver = struct.unpack_from("<IH", b, 4)
                detail[f"type=0x{tid:08X} u16@8={ver}"] += 1
            elif ext == "dds":
                info, wh = dds_info(b)
                detail[info] += 1
                dims[f"{wh[0]}x{wh[1]}"] += 1
            elif b[:4] == b"OggS":
                codec = b[29:35]
                detail[f"codec={codec!r} ch={b[39] if len(b) > 39 else '?'} rate={struct.unpack_from('<I', b, 40)[0] if len(b) > 44 else '?'}"] += 1
        for k, n in magics.most_common(8):
            print(f"   magic {k}: {n}")
        for k, n in detail.most_common(12):
            print(f"   {k}: {n}")
        if dims:
            print("   dims: " + ", ".join(f"{k}({n})" for k, n in dims.most_common(12)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
