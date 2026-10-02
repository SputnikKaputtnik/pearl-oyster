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
