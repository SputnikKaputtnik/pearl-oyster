#!/usr/bin/env python3
"""Static Lua `require` graph of a Spotlight Stories installation.

Usage: lua_requires.py <install_root> <entry> [--mermaid]
  entry e.g. "pearl_vrcam/scripts/app"
Resolves `require "x"` and `require(Platform.package.."/y")` (package substituted from the
entry's first path component). Prints an indented tree (each module expanded once), or a
Mermaid graph with --mermaid. Dynamic requires that cannot be resolved are reported.
"""
import os
import re
import sys

REQ = re.compile(r'require\s*\(?\s*(?:Platform\.package\s*\.\.\s*)?"([^"]+)"\s*\)?')
DYNAMIC = re.compile(r'require\s*\(\s*(?!Platform\.package)(?!")([^)]*)\)')


def strip_comments(src):
    src = re.sub(r"--\[(=*)\[.*?\]\1\]", "", src, flags=re.S)
    return re.sub(r"--[^\n]*", "", src)


def main(argv):
    root, entry = argv[1], argv[2]
    mermaid = "--mermaid" in argv
    package = entry.split("/")[0]
    edges, seen, unresolved = [], set(), []

    def visit(mod, depth):
        path = os.path.join(root, mod + ".lua")
        if not mermaid:
            print("  " * depth + mod + ("" if os.path.exists(path) else "   [MISSING]") +
                  ("   (seen)" if mod in seen else ""))
        if mod in seen or not os.path.exists(path):
            return
        seen.add(mod)
        src = strip_comments(open(path, encoding="latin-1").read())
        for m in DYNAMIC.finditer(src):
            unresolved.append((mod, m.group(1).strip()))
        for m in REQ.finditer(src):
            dep = m.group(1)
            if "Platform.package" in src[max(0, m.start()):m.end()]:
                dep = package + dep
            edges.append((mod, dep))
            visit(dep, depth + 1)

    visit(entry, 0)
    if mermaid:
        print("graph LR")
        for a, b in edges:
            print(f'  {a.replace("/", "_")}["{a}"] --> {b.replace("/", "_")}["{b}"]')
    print(f"\n# {len(seen)} modules, {len(edges)} require edges", file=sys.stderr)
    for mod, expr in unresolved:
        print(f"# dynamic require in {mod}: {expr}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
