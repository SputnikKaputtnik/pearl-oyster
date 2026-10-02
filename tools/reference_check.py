#!/usr/bin/env python3
"""Referential integrity: collect every "package:path.ext" asset URI mentioned in Lua
scripts and binary assets (.mxm/.mxa/.mxb/.pfb/.shd) and check that it resolves on disk
(case-insensitively, as the Windows build does). Also lists files nobody references.

Usage: reference_check.py <install_root> [out.tsv]
Output TSV: uri, resolves(0/1), referenced-by count, first referrer.
"""
import collections
import os
import re
import sys

URI = re.compile(rb"\b(common|story|pearlpackage|pearl_vrcam):([A-Za-z0-9_./\-]+\.[A-Za-z0-9]{2,7})",
                 re.IGNORECASE)
SCAN_EXT = {"lua", "mxm", "mxa", "mxb", "pfb", "shd", "txt"}


def main(argv):
    root = argv[1]
    disk = {}
    for dp, _, fns in os.walk(root):
        for fn in fns:
            rel = os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, "/")
            disk[rel.lower()] = rel
    refs = collections.defaultdict(set)
    for low, rel in disk.items():
        if low.rsplit(".", 1)[-1] not in SCAN_EXT or low.endswith(".manifest"):
            continue
        data = open(os.path.join(root, rel), "rb").read()
        for m in URI.finditer(data):
            uri = (m.group(1) + b"/" + m.group(2)).decode("latin-1").lower()
            refs[uri].add(rel)
    rows = []
    missing = 0
    for uri in sorted(refs):
        ok = uri in disk
        missing += not ok
        rows.append(f"{uri}\t{int(ok)}\t{len(refs[uri])}\t{sorted(refs[uri])[0]}")
    referenced = set(refs)
    content_ext = {"dds", "mxm", "mxa", "mxb", "ogg", "shd", "pfb", "fnt", "mxhrtf"}
    orphans = sorted(r for low, r in disk.items()
                     if low.rsplit(".", 1)[-1] in content_ext and low not in referenced)
    print(f"{len(refs)} distinct URIs referenced, {missing} unresolved")
    for r in rows:
        if r.split("\t")[1] == "0":
            print("  UNRESOLVED " + r)
    by_ext = collections.Counter(o.rsplit(".", 1)[-1] for o in orphans)
    print(f"{len(orphans)} content files never referenced by URI: {dict(by_ext)}")
    if len(argv) > 2:
        with open(argv[2], "w", encoding="utf-8", newline="\n") as f:
            f.write("# uri\tresolves\trefcount\tfirst_referrer\n" + "\n".join(rows) + "\n")
            f.write("# --- unreferenced content files\n" + "\n".join("#ORPHAN\t" + o for o in orphans) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
