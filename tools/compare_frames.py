#!/usr/bin/env python3
"""Compare player frames with reference playblast frames of the original (run07a).

Usage: compare_frames.py <player dir> <reference dir> [--offsets 9,10,11,12] [--pattern f%05d.tga]
For every player frame f%05d.tga, compares against screenshot_%05d.tga at f + offset and prints
the mean absolute difference (0..255, RGB) per offset; the best offset per frame is marked.
"""
import os
import re
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main(argv):
    pdir, rdir = argv[1], argv[2]
    offsets = [10, 11]
    if "--offsets" in argv:
        offsets = [int(x) for x in argv[argv.index("--offsets") + 1].split(",")]
    frames = sorted(int(m.group(1)) for n in os.listdir(pdir) if (m := re.match(r"f(\d+)\.tga$", n)))
    for f in frames:
        a = load(os.path.join(pdir, f"f{f:05d}.tga"))
        res = []
        for o in offsets:
            rp = os.path.join(rdir, f"screenshot_{f + o:05d}.tga")
            if not os.path.exists(rp):
                res.append(None)
                continue
            b = load(rp)
            if b.shape != a.shape:
                res.append(None)
                continue
            res.append(float(np.abs(a - b).mean()))
        valid = [r for r in res if r is not None]
        best = min(valid) if valid else None
        cells = " ".join(f"{o:+d}:{r:6.2f}{'*' if r == best else ' '}" if r is not None else f"{o:+d}:   n/a " for o, r in zip(offsets, res))
        print(f"frame {f:5d}  {cells}")


if __name__ == "__main__":
    main(sys.argv)
