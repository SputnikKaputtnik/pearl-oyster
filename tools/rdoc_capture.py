# Run inside RenderDoc's embedded Python:
#   qrenderdoc.exe --python tools/rdoc_capture.py
# Configuration via environment variables:
#   RDC_EXE, RDC_CWD, RDC_ARGS, RDC_OUT (capture file template), RDC_LOG (text log),
#   RDC_TIMES (comma-separated seconds after launch to trigger captures), RDC_TOTAL (seconds)
# Launches the target with RenderDoc injected, logs API registration (supported or not),
# triggers frame captures at the given times, then posts WM_CLOSE to the target's windows.
import ctypes
import os
import time
from ctypes import wintypes

import traceback

import renderdoc as rd

E = os.environ
log_f = open(E["RDC_LOG"], "w", encoding="utf-8")



def log(*a):
    log_f.write(" ".join(str(x) for x in a) + "\n")
    log_f.flush()


def close_windows(pid):
    user32 = ctypes.windll.user32
    found = []
    proto = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(proto(cb), 0)
    for h in found:
        user32.PostMessageW(h, 0x0010, 0, 0)
    return len(found)


def drain(tc, until):
    while time.time() < until:
        msg = tc.ReceiveMessage(lambda p: None)
        t = msg.type
        if t == rd.TargetControlMessageType.RegisterAPI:
            api = msg.apiUse
            log(f"{time.time() - t0:7.2f} RegisterAPI name={api.name} presenting={api.presenting} "
                f"supported={api.supported} supportMessage={api.supportMessage!r}")
        elif t == rd.TargetControlMessageType.NewCapture:
            c = msg.newCapture
            log(f"{time.time() - t0:7.2f} NewCapture id={c.captureId} frame={c.frameNumber} "
                f"api={c.api} local={c.local} path={c.path}")
        elif t == rd.TargetControlMessageType.CaptureProgress:
            pass
        elif t == rd.TargetControlMessageType.Disconnected:
            log(f"{time.time() - t0:7.2f} Disconnected")
            return False
        elif t != rd.TargetControlMessageType.Noop:
            log(f"{time.time() - t0:7.2f} message {t}")
        time.sleep(0.05)
    return True


def main():
    global t0
    if E.get("RDC_DRYRUN"):
        log("renderdoc API:", [n for n in dir(rd) if "Capture" in n or "Inject" in n or "Target" in n])
        o = rd.CaptureOptions()
        log("CaptureOptions fields:", [n for n in dir(o) if not n.startswith("_")])
        log("ExecuteAndInject doc:", rd.ExecuteAndInject.__doc__)
        log("CreateTargetControl doc:", rd.CreateTargetControl.__doc__)
        log("TargetControl methods:", [n for n in dir(rd.TargetControl) if not n.startswith("_")])
        return
    log("renderdoc API:", [n for n in dir(rd) if "Capture" in n or "Inject" in n or "Target" in n])
    opts = rd.CaptureOptions()
    t0 = time.time()
    res = rd.ExecuteAndInject(E["RDC_EXE"], E["RDC_CWD"], E["RDC_ARGS"], [], E["RDC_OUT"], opts, False)
    log("ExecuteAndInject:", res.result, "ident:", res.ident)
    tc = rd.CreateTargetControl("", res.ident, "pearl-capture", True)
    if tc is None:
        log("CreateTargetControl failed")
    else:
        pid = tc.GetPID()
        log("target pid", pid, "api", tc.GetAPI(), "target", tc.GetTarget())
        alive = True
        for when in [float(x) for x in E.get("RDC_TIMES", "30").split(",")]:
            alive = alive and drain(tc, t0 + when)
            if not alive:
                break
            log(f"{time.time() - t0:7.2f} TriggerCapture")
            tc.TriggerCapture(1)
        if alive:
            alive = drain(tc, t0 + float(E.get("RDC_TOTAL", "75")))
        n = close_windows(pid)
        log(f"{time.time() - t0:7.2f} WM_CLOSE to {n} window(s)")
        if alive:
            drain(tc, time.time() + 10)
        tc.Shutdown()


try:
    main()
except Exception:
    log(traceback.format_exc())
log("done")
log_f.close()
os._exit(0)
