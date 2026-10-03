#!/usr/bin/env python3
"""Recover the exact sequence of Stream reads (element size x count) from a decompiled
Moxie reader function, using the result check that follows each call:

    iVar = (**(code **)(*(longlong *)param_1 + 8))(param_1, this + 0x80, 4);
    if (iVar != 3) { ... }                      -> f32/u32 x 3

Ghidra often drops the count argument; the `!= N` check restores it.
Usage: read_layout.py <decompiled.c> <function-header-substring>
Prints one line per read: target, element size, count, total bytes.
"""
import re
import sys

CALL = re.compile(r"(\w+) = \(\*\*\(code \*\*\)\(\*\(longlong \*\)param_\d \+ 8\)\)\s*\(param_\d,([^;]*)\);")
PLAIN = re.compile(r"\(\*\*\(code \*\*\)\(\*\(longlong \*\)param_\d \+ 8\)\)\s*\(param_\d,([^;]*)\);")
CHECK = re.compile(r"if \((\w+) != (0x[0-9a-f]+|\d+)\)")


def layout(src):
    lines = src.splitlines()
    out = []
    for i, line in enumerate(lines):
        m = CALL.search(line) or PLAIN.search(line)
        if not m:
            continue
        args = [a.strip() for a in (m.group(2) if m.re is CALL else m.group(1)).split(",")]
        var = m.group(1) if m.re is CALL else None
        size = int(args[1], 0) if len(args) > 1 and re.fullmatch(r"0x[0-9a-f]+|\d+", args[1]) else None
        count = args[2] if len(args) > 2 else None
        if count is None and var:
            for nxt in lines[i + 1:i + 3]:
                c = CHECK.search(nxt)
                if c and c.group(1) == var:
                    count = c.group(2)
                    break
        out.append((args[0], size, count or "1?"))
    return out


def main(argv):
    text = open(argv[1], encoding="utf-8", errors="replace").read()
    start = text.index(argv[2])
    end = text.find("\n// ==== ", start + 1)
    body = text[start:end if end > 0 else len(text)]
    total = 0
    for tgt, size, count in layout(body):
        try:
            n = int(count, 0)
        except (TypeError, ValueError):
            n = None
        nbytes = size * n if (size and n) else None
        total += nbytes or 0
        print(f"  {tgt:28s} size {size} x {count} = {nbytes}")
    print(f"  total fixed bytes: {total}")


if __name__ == "__main__":
    main(sys.argv)
