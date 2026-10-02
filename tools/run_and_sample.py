#!/usr/bin/env python3
"""Launch the runtime, sample its threads at given times, then close it politely.

Usage: run_and_sample.py --cwd <dir> --out <dir> --at 10[,15] --seconds 20 -- <exe> [args...]
Writes <out>/run.json (timings, RSS/thread counts, exit) and <out>/sample_<t>.json from
tools/thread_sampler.py (--stack). WM_CLOSE first, terminate only if it does not exit in 10 s.
"""
import argparse
import json
import os
import subprocess
import sys
import time

import psutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from observe_run import close_windows  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cwd", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--at", default="10")
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    os.makedirs(a.out, exist_ok=True)
    rep = {"cmd": cmd, "events": []}
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=a.cwd)
    pp = psutil.Process(p.pid)

    def ev(msg):
        rep["events"].append([round(time.time() - t0, 2), msg])
        print(f"{time.time() - t0:6.1f}s {msg}", flush=True)

    for when in [float(x) for x in a.at.split(",")]:
        while time.time() - t0 < when and p.poll() is None:
            time.sleep(0.2)
        if p.poll() is not None:
            ev(f"exited early rc={p.returncode}")
            break
        ev(f"rss={pp.memory_info().rss / 1e6:.0f}MB threads={pp.num_threads()} cpu={pp.cpu_percent(0.5):.0f}%")
        subprocess.run([sys.executable, os.path.join(HERE, "thread_sampler.py"), str(p.pid), "60", "20",
                        os.path.join(a.out, f"sample_{int(when)}.json"), "--stack"])
    while time.time() - t0 < a.seconds and p.poll() is None:
        time.sleep(0.2)
    if p.poll() is None:
        ev(f"rss={pp.memory_info().rss / 1e6:.0f}MB threads={pp.num_threads()} before close")
        ev(f"WM_CLOSE to {close_windows(p.pid)} window(s)")
        try:
            p.wait(10)
            ev(f"exited rc={p.returncode}")
        except subprocess.TimeoutExpired:
            p.terminate()
            p.wait(10)
            ev("terminated (WM_CLOSE timed out)")
    with open(os.path.join(a.out, "run.json"), "w", encoding="utf-8") as f:
        json.dump(rep, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
