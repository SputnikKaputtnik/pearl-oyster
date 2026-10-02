# Runtime observations (observed runs of the original)

All runs used a writable working copy (`C:\Tools\pearl-work\run`, verified identical to the
master before each run), the exact Steam command line
`win64\storyplayer.exe -package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0`,
working directory = install root. Each run was announced and approved beforehand.
Tools: `tools/observe_run.py` (psutil, no injection), Procmon (`tools/procmon_filter.py`),
before/after `tools/manifest.py verify`, RenderDoc 1.46 (`tools/rdoc_capture.py`).
Curated outputs: `research/runs/`. Raw data (2 GB Procmon log) stays in `C:\Tools\pearl-work\runs`.

| Run | Date | Setup | Result |
|---|---|---|---|
| run01 | 2026-10-03 00:44 | SteamVR not running, 90 s, then hard terminate | `VRInitError_Init_HmdNotFound` (Steam `logs/vrclient_storyplayer.txt`); game **continues in desktop (non-HMD) mode** and plays the story |
| run02 | 2026-10-03 00:50 | SteamVR running (Quest via Link/VD), Procmon, 90 s, `WM_CLOSE` | VR session established (`VRApplication_Scene`, legacy input), clean exit |
| run03 | 2026-10-03 00:59 | RenderDoc injected, capture triggered at 30 s | RenderDoc: `OpenGL supported=False` (legacy/compat context); **process crashed inside renderdoc.dll** (0xC0000005) ~14 s after the trigger. Tool limitation, not a game bug |

## Facts established

* **Module set [F]** (run01 + run02): `storyplayer.exe`, `moxie.v2.shared.windows.dll`,
  `lua51.dll`, `openvr_api.dll`, `PVRTexLib.dll`, `libcurl/libeay32/ssleay32/libssh2.dll`,
  `pthreadVC2.dll`. **Not loaded: `moxie.v1.shared.windows.dll`, `FreeImage.dll`** and the
  bundled 32-bit CRT DLLs (VC++ 2015 x64 CRT comes from System32). With VR:
  `vrclient_x64.dll` plus `d3d11/dxgi` (**[I]** loaded by the SteamVR client for texture
  interop); graphics driver `nvoglv64.dll` (OpenGL); audio `dsound.dll` + `AudioSes.dll`.
* **Working directory**: the install root works; all runtime writes go to the root
  (`log.txt`, `libmoxieclient.tmp`, `persist.lua/.chk`, `saves/`).
* **Bootstrap script [F, corrects Phase 1 inference]**: the engine loads
  **`story/scripts/app.lua`** directly — it never probes `pearl_vrcam/scripts/app.lua`.
  Consequences: Pearl's own `app.lua` (which adds `def.filters` post-effects) is **dead code**,
  and `Time.setErrorCorrectParams(true, 0.25)` from the generic `app.lua` **is active**.
  `pearl_vrcam/scripts/data/override.lua` is not loaded either.
* **Lua load order [F]**: 133 files, identical to the static require tree
  (`research/lua_require_tree.txt`) except for the app script; first `persist.lua`, then
  `story/scripts/app.lua` … data files … the 39 render graphs.
* **Probes for absent files [F]** (informative for a replacement player):
  `version.txt` (root), `common/shaders/ui/solid.shd`, `billboard.shd`, `billboard_2.shd`,
  `saves/.dat`, and for **every audio clip a `.mxs` file before the `.ogg`**
  (e.g. `pearl_landing_desert_ambi_vr.mxs`) → the engine prefers a native `.mxs` audio format
  and falls back to Ogg.
* **Streaming [F]**: assets are loaded per state with prefetch; in 90 s run02 read
  167 `.mxm` (20 MB), 228 `.mxa` (57 MB), 491 `.dds` (394 MB), 142 Pearl shader permutations,
  7 OGGs (streamed, opened when the shot that uses them starts: 3.8 s, 11.7 s, 21.4 s).
* **Writes [F]** (manifest diff + Procmon): `log.txt` (only the 48-byte build header
  `[ 10:22:57 ][ storyplayer.exe ][ Sep 16 2016 ]` — the candidate build logs nothing else),
  `libmoxieclient.tmp`, `persist.lua/.chk`, and **`saves/checkpoint_N.dat/.tga` overwritten
  automatically** each time a `checkpoint` command runs (7 in 90 s). Running from the Steam
  folder therefore always overwrites the user's saves — another reason to run copies only.
* **Story without a player [F]**: in both runs the story advanced through the intro to
  `seq1_shot10`… on its own (≈ 7 checkpoints in 90 s), i.e. the seated/standing check does not
  block when nobody wears the headset.

## Tooling conclusions

* RenderDoc cannot capture Moxie (legacy GL context) and destabilises it → use **apitrace**
  (14.0 installed at `C:\Tools\apitrace\apitrace-14.0-win64`, SHA-256 of the release archive
  `32c70268…78147dc3`)
  (records legacy GL, replays, dumps frames/state) or NVIDIA Nsight Graphics for GL frame
  analysis in Phase 2.
* RenderDoc's UI auto-probes adb devices on start (it ran `adb root` and three `getprop`
  queries against the connected Android/Quest device; nothing installed or started).

## run04 — apitrace (2026-10-03 01:03, SteamVR active, 60 s)

`tools/apitrace_run.py` → `C:\Tools\pearl-work\runs\run04\pearl.trace` (10 MB, 92 558 calls).
Under apitrace the game **stalled after 21 VR frames** (42 eye submits), still in the
startup/preload phase, and had to be terminated; apitrace also caught 50 LuaJIT error
exceptions (`0xE24C4A02`, LuaJIT's SEH-based `error()`/`pcall` mechanism) and flushed on each.
Replays (`glretrace`): snapshots of single-sample targets work; MSAA targets cannot be read
back; a full state dump crashed glretrace (shared-context replay is imperfect:
`wglShareLists` fails, `wglDXLockObjectsNV` unsupported). Raw trace, dump and PNGs stay in
the work dir (they contain rendered Pearl imagery).

Facts from the trace **[F]**:
* Context setup: pixel format RGBA8 + D24S8, double-buffered, **legacy `wglCreateContext`**
  (no `wglCreateContextAttribsARB` → compatibility context), **4 contexts sharing lists**
  (main + loader threads; draw calls observed on threads @0 and @2), `wglSwapIntervalEXT(1)`.
* Driver caps queried at start include `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` (16) and the
  compressed-format list (S3TC + ETC2/EAC + ASTC on this NVIDIA driver).
* Per-eye render target **2612×2852** (= SteamVR recommended size × `-ssaa 1.0`), colour
  `GL_TEXTURE_2D_MULTISAMPLE` RGBA8 (and one RGB target), depth `DEPTH24_STENCIL8`
  renderbuffer, **2× MSAA**, resolved with `glBlitFramebuffer(GL_NEAREST)`.
* Post-processing chain on reduced targets: 1306×1426 (½, MSAA 2×), 653×713 (¼), 326×356 (⅛),
  each pass = clear + full-screen strip + MSAA resolve blit.
* Clear colours per render view exactly as in `pearlpackage/rendergraphs/*.lua`
  (`RV1` 0.5 grey, `RV2` (0.5,0.5,0,1), `RV3` (0,0,0.5,0)).
* Eye hand-off: `wglDXLockObjectsNV` + `glCopyImageSubData` of each resolved eye texture into
  a D3D11-shared texture (SteamVR's OpenGL submit path), then `glFlush`.
* Desktop window shows a separate **title card** ("Please sit down, put on your VR headset…",
  key help `C` calibrate camera, `M` mirror display, `V` toggle VR), not the eye view.
* Texture sampling: all 67 texture parameter sets use `GL_TEXTURE_MIN_FILTER = GL_LINEAR`
  and anisotropy 1; **no `glGenerateMipmap` and no mip levels uploaded** → the original
  samples every texture **without mipmaps** (expect shimmer on minified detail). DXT5 is
  uploaded compressed (`glCompressedTexImage2D`); the ETC2 textures appear as uncompressed
  `GL_RGB` uploads (512², 1024²) → decoded on the CPU **[I: PVRTexLib]**.
* Draw path: VAOs + `glDrawElements(GL_UNSIGNED_SHORT)` for meshes, client-memory vertex
  arrays for full-screen quads, `glUniformMatrix4fv` dominant (15 204 calls in 21 frames),
  `GL_FRAMEBUFFER_SRGB` off (gamma in shader), 104 shader objects compiled.
* A scene draw at call 92274: depth test `GL_LESS`, culling on, blending
  `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, half-resolution view.

Next capture attempt (needs approval): trace in **desktop/mono mode** (no SteamVR session),
which avoids the DX-interop submit path where the stall happened, to capture actual story
frames; and/or a proxy `openvr_api.dll` that dumps eye textures directly.

## run05 — apitrace in desktop mode (2026-10-03 ~01:15, 75 s)

VR hidden for this process only (`VR_OVERRIDE` pointing to an empty directory; SteamVR kept
running, no system change). Confirmed: no `vrclient` session was opened (no new entry in
Steam's `vrclient_storyplayer.txt`). Result: **the same stall** — 25 desktop frames
(title card + preload), 9.8 MB trace, again exactly 50 LuaJIT exceptions, `WM_CLOSE`
ignored, terminated.

Diagnosis **[F]**: in run04 and run05 the loader thread (`@2`, shared context) issues exactly
2131 calls (buffer uploads, 26 shader compiles, many `glFinish`) and then **no further GL
call** (last at call 7569 of 34 929); the main thread keeps rendering title/preload frames and
then stops. **[I]** Under apitrace the resource loader blocks (cause unknown: candidate
interactions are apitrace's global call lock with the multi-context loader, or its handling
of LuaJIT's SEH-based errors). This happens independently of VR, so the earlier VR stall was
not the DX-interop path. Without apitrace the same build renders the story in the headset
(run02, user-confirmed visually).

Consequence: whole-API interception of the original is currently not usable beyond startup.
Options for real reference frames, in order of preference:
1. the engine's own `-rendertodisk` / `-playblast` / `-fixedtimestep` (syntax via Ghidra first);
2. a proxy `openvr_api.dll` that reads back the submitted eye textures (no GL interception);
3. diagnose the apitrace hang with a native stack dump of both threads (needs a debugger/
   procdump run).

## run06 — built-in capture test (2026-10-03, mono, 45 s)

Command: Steam line + `-mono -fixedtimestep 33.3333 -playblast <abs dir> -record <abs csv>`.

* `-mono` works: desktop window shows the film (user-confirmed), no VR session.
* **Fixed step works [F]**: `pose.csv` has 5337 rows with `time` in µs advancing by exactly
  33 333 per frame; story time reached 177.9 s (`Main:seq4_shot10_Part2`) in 45 s wall time
  (mono renders ≈118 fps, so the story runs ≈4× real time under a fixed step).
* **`-record` works [F]**: CSV header `time,pos,rot,scale,marker`; `marker` = `FSM:state`.
  In mono without head input the recorded pose is constant (pos `-3,110,45.875`, identity
  rotation) for the whole run — **[?]** which transform is recorded (rig root vs. final camera).
* **`-playblast` produced nothing [F]**: no TGA, and the target directory was not even created.
  Code findings: `captureStart` runs once in `LApplication::onInitialize`; the per-graph capture
  is configured only on the *active render graph* (`RenderManager+0x90`).
  `setActiveRenderGraph(RenderGraph*)` re-applies capture to a new graph, but
  `setActiveRenderGraph(String)` (by name) only swaps the pointer — Pearl switches graphs per
  shot. Why the directory was not created is still open (absolute path vs. the engine's virtual
  file system is the main suspect).
* `-rendertodisk on` only sets `Platform.renderToDisk`, which gates a `renderToDisk` *story
  command* (`Condition.renderToDiskMode`); Pearl's `states.lua` contains no such command, so the
  switch has no effect for Pearl.

## run07 — capture path rule (2026-10-03, mono, 2 × 20 s)

* **run07a**: `-playblast C:\…un07arames` with the directory **created beforehand** →
  **2374 TGA frames** (1280×720, 24-bit), continuous across shot/render-graph changes
  (contact sheet: seated-hint card, PEARL title, car interior seq1/seq2 with changing light).
  26 frames identical to their predecessor (black/static intro frames).
* **run07b**: relative path `capturesun07b` → nothing written, no directory created.
* Conclusion: the run06 failure was only the missing directory; the render-graph hypothesis was
  wrong. Rule: pre-create the target directory and pass an absolute path.

## run08 — built-in capture in VR mode (2026-10-03 01:31, 20 s)

Command: Steam line (SteamVR running) + `-fixedtimestep 33.3333 -playblast <pre-created abs dir>
-record <abs csv>`. VR session established (`vrclient_x64.dll` loaded).

* Exactly **two files** written: `screenshot_eye0_00000.tga` and `screenshot_eye1_00001.tga`
  (2612×2852, 24-bit, both **completely black** = first preload frame). Note the frame counter
  increments per eye, not per frame **[F]**.
* Immediately afterwards the process **spins**: from t = 2.3 s CPU ≈ 200 % (two busy threads),
  RSS frozen at 137 MB, no further frames, `pose.csv` header only, `WM_CLOSE` ignored →
  terminated. (Normal VR run02: RSS grows to 340 MB while loading, CPU settles at 25–40 %.)
* The same capture options work in mono (run07a). Which option triggers the VR spin
  (`-playblast`, `-fixedtimestep`, `-record`, or their combination with the compositor's frame
  pacing) is not isolated yet.

## run09 — reproduction of run08 with thread sampling (2026-10-03)

Same command as run08. This time **no frame at all** was written; the hang came even earlier
(22 threads, RSS 157 MB — the loader threads never started; normal VR run: ~50 threads, 340 MB).
`tools/thread_sampler.py` at 8 s and 15.8 s (100 samples each), offsets resolved against this
machine's system DLLs:

| Thread | CPU in 15 s | Where (RIP) |
|---|---|---|
| A | 7.6 s (≈100 %) | `win32u!NtGdiDdDDIEscape` (65 %) / `NtGdiDdDDIGetDeviceState` (30 %), occasionally `nvoglv64.dll` — the NVIDIA OpenGL driver polling the kernel graphics stack |
| B | 7.6 s (≈100 %) | `ntdll!ZwQueryInformationThread` (87 %) / `ZwDelayExecution` — a `Sleep(0)`-style poll of another thread's state |

No TDR / display-driver event and no crash in the event logs. **[I]** The render thread is
busy-waiting inside the GL driver (fence/present/interop wait) while a second thread polls it —
a deadlock-like stall at the very first VR frame(s). Which Moxie call leads there is not yet
known (no stack data); the sampler now has a heuristic stack scan (`--stack`) for the next run.
Since a plain VR run (run02) works, one of `-fixedtimestep`, `-playblast`, `-record` (or their
combination) triggers it.

## run10 — option isolation in VR, and the SteamVR state (2026-10-03 01:37–02:0x)

`tools/run_and_sample.py` (launch, sample with stack scan at 10 s, WM_CLOSE at 20 s).

| Run | Options (besides Steam line) | Result |
|---|---|---|
| 10a | `-fixedtimestep 33.3333` | hang: 2 busy threads (GL driver D3DKMT poll / thread poll), main thread blocked in `WaitForSingleObject` under the VR frame loop (`moxie+0x18d243` in `FUN_18018d070` → compositor) |
| 10b | `-playblast <dir>` | same hang (main thread blocked at a different place: `moxie+0x20b5cb … 0x1987bb`), no frame |
| — | user restarted SteamVR (new vrserver PID) | |
| 10c (control) | none (exact Steam line, as run02) | **healthy**: 53 threads, RSS 219→248 MB, CPU ≈31 %, clean exit on WM_CLOSE |

Timeline: the only good VR run before (run02, 00:50) preceded the RenderDoc crash (run03,
00:59) that killed the game **during an active VR session**; every VR run afterwards (04, 08,
09, 10a, 10b) hung at the first frame regardless of options, while mono runs worked.
**[I, strong]** SteamVR/compositor was left in a wedged state by that crash; runs 08–10b say
nothing about `-playblast`/`-fixedtimestep` in VR. Practical rule: after any crash/kill of a
VR session, restart SteamVR before the next VR measurement. Not yet tested in a clean
environment: VR + `-playblast`/`-fixedtimestep`.

## run11 — built-in capture in VR, clean SteamVR (2026-10-03)

Steam line + `-fixedtimestep 33.3333 -playblast <pre-created abs dir>`, SteamVR freshly
restarted. **Works**: 496 TGAs = **248 stereo pairs** (`screenshot_eye0_<2n>` /
`screenshot_eye1_<2n+1>`), each 2612×2852 24-bit (SteamVR recommended size × `-ssaa 1.0`),
11 GB in 20 s; process healthy (52 threads, CPU ≈75 %), clean exit on WM_CLOSE (rc 0).
Content: intro (night desert, multilingual "Please sit down" prompt, seat icon turning green),
with visible stereo disparity; only the very first pair is black.

Throughput ≈12 stereo frames/s (disk-bound: ≈45 MB per pair); with the fixed step the story
advanced 248/30 ≈ 8.3 s. A full-film stereo capture would be ≈10 500 pairs ≈ 470 GB — use
selected shots (`-loadfile`/`-checkpoint`) or a lower `-ssaa` for overview captures.

**Conclusion:** the built-in capture yields true per-eye reference frames from the original
build without modifying it. Requirements: pre-created absolute directory; SteamVR in a clean
state (restart after any crashed/killed VR session).
