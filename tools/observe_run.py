#!/usr/bin/env python3
"""Launch a program and observe it from the outside (no injection, no admin needed).

Usage:
    observe_run.py --cwd <dir> --seconds N --out <report.json> -- <exe> [args...]

Records, while the process (and its children) run:
  * process cwd, command line, child processes
  * loaded modules (psutil memory_maps, sampled at several points)
  * files seen open (psutil open_files, polled every ~20 ms; short opens can be missed)
  * thread count / CPU / RSS samples
After N seconds the process tree is terminated (unless it exited earlier).
Combine with `manifest.py create` before/after to get the exact set of written files.
"""
import argparse
import json
import subprocess
import sys
import time

import psutil


def close_windows(pid):
    """Post WM_CLOSE to every top-level window of `pid` (Windows only)."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    for h in found:
        user32.PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE
    return len(found)


def snapshot_modules(p):
    try:
        return sorted({m.path for m in p.memory_maps(grouped=True) if m.path})
    except (psutil.Error, OSError):
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cwd", required=True)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd

    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=a.cwd)
    p = psutil.Process(proc.pid)
    rep = {"cmd": cmd, "cwd_requested": a.cwd, "pid": proc.pid, "start": t0,
           "files": {}, "modules": {}, "children": {}, "samples": []}
    try:
        rep["cwd_observed"] = p.cwd()
    except psutil.Error as e:
        rep["cwd_observed"] = f"error: {e}"
    module_points = [1, 3, 8, 20, a.seconds - 2]
    next_mod = 0
    last_sample = 0
    while time.time() - t0 < a.seconds and proc.poll() is None:
        el = time.time() - t0
        procs = [p]
        try:
            procs += p.children(recursive=True)
        except psutil.Error:
            pass
        for q in procs:
            try:
                if q.pid != p.pid and q.pid not in rep["children"]:
                    rep["children"][q.pid] = {"name": q.name(), "cmd": q.cmdline(), "t": round(el, 3)}
                for f in q.open_files():
                    rep["files"].setdefault(f.path, round(el, 3))
            except psutil.Error:
                pass
        if next_mod < len(module_points) and el >= module_points[next_mod]:
            for m in snapshot_modules(p):
                rep["modules"].setdefault(m, round(el, 3))
            next_mod += 1
        if el - last_sample >= 1.0:
            try:
                rep["samples"].append({"t": round(el, 2), "threads": p.num_threads(),
                                       "rss_mb": round(p.memory_info().rss / 1e6, 1),
                                       "cpu": p.cpu_percent()})
            except psutil.Error:
                pass
            last_sample = el
        time.sleep(0.02)
    rep["exit_code_before_close"] = proc.poll()
    if proc.poll() is None:
        # polite shutdown first (lets the runtime flush its log and save state)
        n = close_windows(proc.pid)
        try:
            proc.wait(15)
            rep["closed_by"] = f"WM_CLOSE to {n} window(s)"
        except subprocess.TimeoutExpired:
            rep["closed_by"] = "terminate (WM_CLOSE timed out)"
    if proc.poll() is None:
        for q in [p] + p.children(recursive=True):
            try:
                q.terminate()
            except psutil.Error:
                pass
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
    rep["duration"] = round(time.time() - t0, 2)
    rep["files"] = dict(sorted(rep["files"].items(), key=lambda kv: kv[1]))
    rep["modules"] = dict(sorted(rep["modules"].items(), key=lambda kv: kv[1]))
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(rep, f, indent=1)
    print(f"ran {rep['duration']} s, {rep.get('closed_by', 'exited by itself')}, "
          f"{len(rep['files'])} files, {len(rep['modules'])} modules, {len(rep['children'])} children")
    return 0


if __name__ == "__main__":
    sys.exit(main())
