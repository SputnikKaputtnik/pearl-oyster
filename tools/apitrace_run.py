#!/usr/bin/env python3
"""Record an OpenGL apitrace of the original runtime for a fixed time, then close it politely.

Usage:
    apitrace_run.py --cwd <dir> --seconds N --trace <out.trace> --log <out.json> -- <exe> [args...]

Runs `apitrace trace --api gl --output <trace> <exe> args` (DLL injection, nothing on disk is
modified), waits for the target process, posts WM_CLOSE after N seconds (terminate only if it
does not exit within 20 s), and records timings/exit codes.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

import psutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from observe_run import close_windows  # noqa: E402

APITRACE = shutil.which("apitrace") or r"C:\Tools\apitrace\apitrace-14.0-win64\bin\apitrace.exe"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cwd", required=True)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--trace", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    exe_name = os.path.basename(cmd[0]).lower()
    rep = {"apitrace": APITRACE, "cmd": cmd, "events": []}
    t0 = time.time()

    def ev(msg):
        rep["events"].append([round(time.time() - t0, 2), msg])
        print(f"{time.time() - t0:7.2f} {msg}", flush=True)

    tracer = subprocess.Popen([APITRACE, "trace", "--api", "gl", "--output", a.trace] + cmd,
                              cwd=a.cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    target = None
    while time.time() - t0 < 30 and target is None:
        for p in psutil.process_iter(["name", "create_time"]):
            if (p.info["name"] or "").lower() == exe_name and p.info["create_time"] >= t0 - 1:
                target = p
                break
        time.sleep(0.2)
    if target is None:
        ev("target process not found")
    else:
        ev(f"target pid {target.pid}")
        while time.time() - t0 < a.seconds and target.is_running():
            time.sleep(0.5)
        if target.is_running():
            ev(f"WM_CLOSE to {close_windows(target.pid)} window(s)")
            try:
                target.wait(20)
                ev("target exited")
            except psutil.TimeoutExpired:
                target.terminate()
                ev("target terminated (WM_CLOSE timed out)")
        else:
            ev("target exited before close")
    out, _ = tracer.communicate(timeout=120)
    rep["apitrace_output"] = out
    rep["apitrace_rc"] = tracer.returncode
    rep["trace_bytes"] = os.path.getsize(a.trace) if os.path.exists(a.trace) else None
    ev(f"apitrace rc={tracer.returncode}, trace {rep['trace_bytes']} bytes")
    with open(a.log, "w", encoding="utf-8") as f:
        json.dump(rep, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
