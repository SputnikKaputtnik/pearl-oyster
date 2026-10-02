#!/usr/bin/env python3
"""Dump a PE's export names, demangled with Windows' dbghelp!UnDecorateSymbolName.

Usage: demangle_exports.py <file.dll> [out.tsv]
Output (TSV): ordinal, mangled name, demangled name. Read-only; Windows only.
"""
import ctypes
import sys

import pefile

_dbghelp = ctypes.windll.dbghelp
_dbghelp.UnDecorateSymbolName.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
UNDNAME_COMPLETE = 0x0000


def undecorate(name):
    if not name.startswith("?"):
        return name
    buf = ctypes.create_string_buffer(4096)
    n = _dbghelp.UnDecorateSymbolName(name.encode(), buf, len(buf), UNDNAME_COMPLETE)
    return buf.value.decode(errors="replace") if n else name


def main(argv):
    pe = pefile.PE(argv[1], fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    rows = []
    for s in (exp.symbols if exp else []):
        name = s.name.decode() if s.name else f"#{s.ordinal}"
        rows.append(f"{s.ordinal}\t{name}\t{undecorate(name)}")
    out = open(argv[2], "w", encoding="utf-8", newline="\n") if len(argv) > 2 else sys.stdout
    out.write("# ordinal\tmangled\tdemangled\n")
    out.write("\n".join(rows) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
