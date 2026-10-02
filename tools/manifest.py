#!/usr/bin/env python3
"""Preservation manifest: relative path, size, SHA-256 (and mtime) for every file.

Usage:
    manifest.py create <root> <out.tsv>      write a manifest of <root>
    manifest.py verify <root> <manifest.tsv> re-hash <root> and compare
    manifest.py diff <a.tsv> <b.tsv>          compare two manifests

Read-only with respect to <root>: files are opened 'rb' and nothing else is touched.
Paths are written with forward slashes, sorted case-insensitively, UTF-8.
"""
import hashlib
import os
import sys
import time

HEADER = "# path\tsize\tsha256\tmtime_utc"

# Files the runtime/Steam writes into the install dir after installation.
# They are recorded but flagged, since they are user state, not shipped content.
VOLATILE_PREFIXES = ("log.txt", "persist.lua", "persist.chk", "libmoxieclient.tmp",
                     "saves/", "steam_shader_cache/")


def sha256_file(path, bufsize=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def walk(root):
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            out.append((rel, full))
    out.sort(key=lambda t: t[0].lower())
    return out


def create(root, out_path):
    rows = []
    total = 0
    files = walk(root)
    t0 = time.time()
    for i, (rel, full) in enumerate(files):
        st = os.stat(full)
        digest = sha256_file(full)
        mtime = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(st.st_mtime))
        rows.append(f"{rel}\t{st.st_size}\t{digest}\t{mtime}")
        total += st.st_size
        if (i + 1) % 500 == 0:
            print(f"  {i + 1}/{len(files)} files, {total / 1e9:.2f} GB, {time.time() - t0:.0f}s",
                  file=sys.stderr)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# root: {os.path.abspath(root)}\n")
        f.write(f"# files: {len(rows)}  bytes: {total}\n")
        f.write(HEADER + "\n")
        for r in rows:
            f.write(r + "\n")
    print(f"{len(rows)} files, {total} bytes -> {out_path}")


def load(path):
    d = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            rel, size, digest, *_ = line.rstrip("\n").split("\t")
            d[rel] = (int(size), digest)
    return d


def compare(a, b, label_a="a", label_b="b"):
    bad = 0
    for rel in sorted(set(a) | set(b), key=str.lower):
        vol = " (volatile)" if rel.lower().startswith(VOLATILE_PREFIXES) else ""
        if rel not in b:
            print(f"ONLY-IN-{label_a}{vol}: {rel}")
            bad += 1
        elif rel not in a:
            print(f"ONLY-IN-{label_b}{vol}: {rel}")
            bad += 1
        elif a[rel] != b[rel]:
            print(f"DIFFERS{vol}: {rel}  {a[rel]} vs {b[rel]}")
            bad += 1
    print(f"{len(a)} vs {len(b)} entries, {bad} differences")
    return bad


def main(argv):
    if len(argv) != 4 or argv[1] not in ("create", "verify", "diff"):
        print(__doc__)
        return 2
    cmd, x, y = argv[1:]
    if cmd == "create":
        create(x, y)
        return 0
    if cmd == "verify":
        tmp = {}
        for rel, full in walk(x):
            tmp[rel] = (os.stat(full).st_size, sha256_file(full))
        return 1 if compare(load(y), tmp, "manifest", "disk") else 0
    return 1 if compare(load(x), load(y), "a", "b") else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
