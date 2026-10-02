#!/usr/bin/env python3
"""Cross-check Spotlight package `.manifest` build records against files on disk.

Each manifest line looks like
    {"pkg:path/out.ext",{{"pkg:path/out.ext",<built_unix>},{"pkg:path/src.ext",<src_unix>}}}
i.e. output asset, its build timestamp, and the source asset it was built from.

Usage: package_manifest_check.py <install_root> [package ...]
Reports outputs listed but missing on disk, and files on disk not listed.
"""
import os
import re
import sys

LINE = re.compile(r'\{"([^"]+)",\{\{"([^"]+)",(\d+)\},\{"([^"]+)",(\d+)\}')


def main(argv):
    root = argv[1]
    pkgs = argv[2:] or [d for d in sorted(os.listdir(root))
                        if os.path.isfile(os.path.join(root, d, ".manifest"))]
    for pkg in pkgs:
        text = open(os.path.join(root, pkg, ".manifest"), encoding="latin-1").read()
        listed = {}
        for m in LINE.finditer(text):
            out = m.group(1)
            p, rel = out.split(":", 1)
            listed[(p.lower(), rel.lower())] = m.group(4)
        on_disk = set()
        for dp, _, fns in os.walk(os.path.join(root, pkg)):
            for fn in fns:
                rel = os.path.relpath(os.path.join(dp, fn), os.path.join(root, pkg)).replace(os.sep, "/")
                if rel.startswith(".") or rel.lower() in ("modelmapping.txt", "_modelmapping.lck", ".ds_store"):
                    continue
                on_disk.add((pkg.lower(), rel.lower()))
        foreign = {k for k in listed if k[0] != pkg.lower()}
        missing = sorted(k for k in listed if k[0] == pkg.lower() and k not in on_disk)
        unlisted = sorted(on_disk - set(listed))
        print(f"## {pkg}: {len(listed)} manifest entries, {len(on_disk)} files on disk")
        if foreign:
            print(f"   entries naming another package: {len(foreign)}")
        for k in missing:
            print(f"   LISTED-NOT-ON-DISK: {k[0]}:{k[1]}  (source {listed[k]})")
        for k in unlisted:
            print(f"   ON-DISK-NOT-LISTED: {k[0]}:{k[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
