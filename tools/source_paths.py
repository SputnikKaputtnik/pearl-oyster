#!/usr/bin/env python3
"""List the original build's source file paths embedded in a binary (assert/log strings).

Usage: source_paths.py <binary> [out.txt]
Prints normalized paths (prefix up to the project dir stripped) and a per-directory count.
"""
import collections
import re
import sys

BS = re.escape("\\")
PAT = re.compile(rb"[A-Za-z]:" + BS.encode() + rb"[\x21-\x7e]+?\.(?:cpp|cc|c|h|hpp|inl|mm)\b")


def main(argv):
    data = open(argv[1], "rb").read()
    paths = sorted({m.group(0).decode("latin-1").replace("\\", "/") for m in PAT.finditer(data)})
    norm = sorted({re.sub(r"^.*?/\.\./\.\./", "", p) for p in paths})
    out = "\n".join(norm) + "\n"
    if len(argv) > 2:
        open(argv[2], "w", encoding="utf-8", newline="\n").write(out)
    counts = collections.Counter(n.rsplit("/", 1)[0] for n in norm)
    print(f"{len(norm)} source files")
    for k, n in sorted(counts.items()):
        print(f"  {n:3d} {k}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
