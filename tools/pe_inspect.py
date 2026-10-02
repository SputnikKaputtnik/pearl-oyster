#!/usr/bin/env python3
"""Static PE summary: machine, timestamp, subsystem, sections, version info,
imports (per DLL) and exports. Read-only.

Usage:
    pe_inspect.py <file.dll|exe> [...]            human-readable report
    pe_inspect.py --exports-only <file>           one export name per line
"""
import datetime
import sys

import pefile

MACHINES = {0x14c: "x86 (i386)", 0x8664: "x86-64 (AMD64)", 0xaa64: "ARM64", 0x1c4: "ARMv7"}


def version_info(pe):
    out = {}
    for fi in getattr(pe, "FileInfo", []) or []:
        for entry in fi:
            for st in getattr(entry, "StringTable", []) or []:
                for k, v in st.entries.items():
                    out[k.decode(errors="replace")] = v.decode(errors="replace")
    return out


def report(path):
    pe = pefile.PE(path, fast_load=False)
    fh = pe.FILE_HEADER
    oh = pe.OPTIONAL_HEADER
    ts = datetime.datetime.fromtimestamp(fh.TimeDateStamp, datetime.timezone.utc)
    print(f"### {path}")
    print(f"machine     : {MACHINES.get(fh.Machine, hex(fh.Machine))}")
    print(f"timestamp   : {ts:%Y-%m-%d %H:%M:%S} UTC (0x{fh.TimeDateStamp:08x})")
    print(f"subsystem   : {pefile.SUBSYSTEM_TYPE.get(oh.Subsystem, oh.Subsystem)}")
    print(f"image base  : 0x{oh.ImageBase:x}  entry RVA 0x{oh.AddressOfEntryPoint:x}")
    print(f"linker      : {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}")
    dbg = getattr(pe, "DIRECTORY_ENTRY_DEBUG", [])
    for d in dbg:
        e = getattr(d, "entry", None)
        if e is not None and hasattr(e, "PdbFileName"):
            pdb = e.PdbFileName.rstrip(b"\0").decode(errors="replace")
            print(f"pdb         : {pdb}")
    for k, v in version_info(pe).items():
        print(f"version     : {k} = {v}")
    secs = [(s.Name.rstrip(b"\0").decode(errors="replace"), s.Misc_VirtualSize) for s in pe.sections]
    print("sections    : " + ", ".join(f"{n}({sz:#x})" for n, sz in secs))
    for imp in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        names = [(i.name.decode() if i.name else f"#{i.ordinal}") for i in imp.imports]
        print(f"import {imp.dll.decode()} ({len(names)}): " + ", ".join(names))
    for imp in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        names = [(i.name.decode() if i.name else f"#{i.ordinal}") for i in imp.imports]
        print(f"delay-import {imp.dll.decode()} ({len(names)}): " + ", ".join(names))
    exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if exp:
        names = [(s.name.decode() if s.name else f"#{s.ordinal}") for s in exp.symbols]
        print(f"exports ({len(names)}): " + ", ".join(names))
    print()


def main(argv):
    if len(argv) >= 3 and argv[1] == "--exports-only":
        pe = pefile.PE(argv[2])
        exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
        for s in (exp.symbols if exp else []):
            print(s.name.decode() if s.name else f"#{s.ordinal}")
        return 0
    for p in argv[1:]:
        report(p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
