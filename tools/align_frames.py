#!/usr/bin/env python3
"""Align reference frames of the original (playblast TGAs) with frames rendered by our viewer.

For every reference frame in a range, find the rendered frame with the smallest mean squared
error (both downscaled to the rendered size). Prints the best match per reference frame and a
linear fit  ref_frame = offset + slope * rendered_index, which gives the state's start frame in
the reference run.

Usage: align_frames.py <ref_dir> <first> <last> <rendered_glob> [--step N]
"""
import glob
import sys

import numpy as np
from PIL import Image


def load(path, size=None):
    im = Image.open(path).convert("RGB")
    if size and im.size != size:
        im = im.resize(size, Image.BILINEAR)
    return np.asarray(im, dtype=np.float32)


def main(argv):
    ref_dir, first, last, pattern = argv[1], int(argv[2]), int(argv[3]), argv[4]
    step = int(argv[argv.index("--step") + 1]) if "--step" in argv else 1
    rendered = sorted(glob.glob(pattern))
    if not rendered:
        raise SystemExit("no rendered frames")
    size = Image.open(rendered[0]).size
    rs = np.stack([load(p) for p in rendered])
    pairs = []
    for f in range(first, last + 1, step):
        ref = load(f"{ref_dir}/screenshot_{f:05d}.tga", size)
        mse = ((rs - ref) ** 2).mean(axis=(1, 2, 3))
        k = int(mse.argmin())
        second = np.partition(mse, 1)[1] if len(mse) > 1 else mse[k]
        pairs.append((f, k, float(mse[k]), float(second)))
        print(f"ref {f:5d} -> rendered {k:4d}  mse {mse[k]:8.2f}  (next best {second:8.2f})")
    a = np.array([[p[1], 1.0] for p in pairs])
    b = np.array([p[0] for p in pairs], dtype=float)
    (slope, off), *_ = np.linalg.lstsq(a, b, rcond=None)
    print(f"fit: ref_frame = {off:.2f} + {slope:.4f} * rendered_index")


if __name__ == "__main__":
    main(sys.argv)
