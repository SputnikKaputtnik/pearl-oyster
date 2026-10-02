#!/usr/bin/env python3
"""Sample the instruction pointers (RIP) of all threads of a running 64-bit process.

Usage: thread_sampler.py <pid> [samples=50] [interval_ms=20] [out.json]
Each sample: SuspendThread → GetThreadContext(CONTEXT_CONTROL) → ResumeThread, for every
thread. RIPs are mapped to module+offset (EnumProcessModulesEx/GetModuleInformation) and
aggregated per thread, plus the per-thread CPU time delta to show which threads are busy.
Purely observational: the target is only paused for microseconds per sample.
"""
import collections
import ctypes
import json
import sys
import time
from ctypes import wintypes

import psutil

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

THREAD_ALL = 0x0002 | 0x0008 | 0x0010 | 0x0040  # suspend/resume, get context, query info
PROCESS_QUERY_VM = 0x0400 | 0x0010
CONTEXT_AMD64 = 0x00100000
CONTEXT_CONTROL = CONTEXT_AMD64 | 0x1


class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_uint64), ("High", ctypes.c_int64)]


class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [
        ("P1Home", ctypes.c_uint64), ("P2Home", ctypes.c_uint64), ("P3Home", ctypes.c_uint64),
        ("P4Home", ctypes.c_uint64), ("P5Home", ctypes.c_uint64), ("P6Home", ctypes.c_uint64),
        ("ContextFlags", wintypes.DWORD), ("MxCsr", wintypes.DWORD),
        ("SegCs", wintypes.WORD), ("SegDs", wintypes.WORD), ("SegEs", wintypes.WORD),
        ("SegFs", wintypes.WORD), ("SegGs", wintypes.WORD), ("SegSs", wintypes.WORD),
        ("EFlags", wintypes.DWORD),
        ("Dr0", ctypes.c_uint64), ("Dr1", ctypes.c_uint64), ("Dr2", ctypes.c_uint64),
        ("Dr3", ctypes.c_uint64), ("Dr6", ctypes.c_uint64), ("Dr7", ctypes.c_uint64),
        ("Rax", ctypes.c_uint64), ("Rcx", ctypes.c_uint64), ("Rdx", ctypes.c_uint64),
        ("Rbx", ctypes.c_uint64), ("Rsp", ctypes.c_uint64), ("Rbp", ctypes.c_uint64),
        ("Rsi", ctypes.c_uint64), ("Rdi", ctypes.c_uint64), ("R8", ctypes.c_uint64),
        ("R9", ctypes.c_uint64), ("R10", ctypes.c_uint64), ("R11", ctypes.c_uint64),
        ("R12", ctypes.c_uint64), ("R13", ctypes.c_uint64), ("R14", ctypes.c_uint64),
        ("R15", ctypes.c_uint64), ("Rip", ctypes.c_uint64),
        ("FltSave", ctypes.c_byte * 512),
        ("VectorRegister", M128A * 26), ("VectorControl", ctypes.c_uint64),
        ("DebugControl", ctypes.c_uint64), ("LastBranchToRip", ctypes.c_uint64),
        ("LastBranchFromRip", ctypes.c_uint64), ("LastExceptionToRip", ctypes.c_uint64),
        ("LastExceptionFromRip", ctypes.c_uint64),
    ]


class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wintypes.DWORD),
                ("EntryPoint", ctypes.c_void_p)]


k32.OpenThread.restype = wintypes.HANDLE
k32.OpenProcess.restype = wintypes.HANDLE
k32.SuspendThread.argtypes = [wintypes.HANDLE]
k32.ResumeThread.argtypes = [wintypes.HANDLE]
k32.GetThreadContext.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
k32.CloseHandle.argtypes = [wintypes.HANDLE]


def modules(pid):
    h = k32.OpenProcess(PROCESS_QUERY_VM, False, pid)
    arr = (ctypes.c_void_p * 1024)()
    needed = wintypes.DWORD()
    psapi.EnumProcessModulesEx(h, arr, ctypes.sizeof(arr), ctypes.byref(needed), 3)
    out = []
    for i in range(needed.value // ctypes.sizeof(ctypes.c_void_p)):
        name = ctypes.create_unicode_buffer(260)
        psapi.GetModuleBaseNameW(h, ctypes.c_void_p(arr[i]), name, 260)
        mi = MODULEINFO()
        psapi.GetModuleInformation(h, ctypes.c_void_p(arr[i]), ctypes.byref(mi), ctypes.sizeof(mi))
        out.append((mi.lpBaseOfDll or 0, mi.SizeOfImage, name.value))
    k32.CloseHandle(h)
    return out


def where(rip, mods):
    for base, size, name in mods:
        if base <= rip < base + size:
            return f"{name}+0x{rip - base:x}"
    return f"0x{rip:x}"


def main(argv):
    pid = int(argv[1])
    n = int(argv[2]) if len(argv) > 2 else 50
    interval = (int(argv[3]) if len(argv) > 3 else 20) / 1000
    proc = psutil.Process(pid)
    mods = modules(pid)
    cpu0 = {t.id: t.user_time + t.system_time for t in proc.threads()}
    hits = collections.defaultdict(collections.Counter)
    ctx = CONTEXT()
    for _ in range(n):
        for t in proc.threads():
            h = k32.OpenThread(THREAD_ALL, False, t.id)
            if not h:
                continue
            if k32.SuspendThread(h) != 0xFFFFFFFF:
                ctx.ContextFlags = CONTEXT_CONTROL
                if k32.GetThreadContext(h, ctypes.byref(ctx)):
                    hits[t.id][where(ctx.Rip, mods)] += 1
                k32.ResumeThread(h)
            k32.CloseHandle(h)
        time.sleep(interval)
    cpu1 = {t.id: t.user_time + t.system_time for t in proc.threads()}
    rep = []
    for tid, c in hits.items():
        busy = round(cpu1.get(tid, 0) - cpu0.get(tid, 0), 3)
        rep.append({"tid": tid, "cpu_s": busy, "top": c.most_common(8)})
    rep.sort(key=lambda r: -r["cpu_s"])
    for r in rep[:8]:
        print(f"tid {r['tid']:6d} cpu {r['cpu_s']:6.2f}s  " + "  ".join(f"{k} x{v}" for k, v in r["top"][:4]))
    if len(argv) > 4:
        json.dump({"pid": pid, "modules": [(hex(b), s, nm) for b, s, nm in mods], "threads": rep},
                  open(argv[4], "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
