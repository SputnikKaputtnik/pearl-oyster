# Moxie v2 command line (Steam build 1340090) — derived from code

Source: Ghidra 12.1.3 decompilation of `moxie.v2.shared.windows.dll`
(`MOXIE::LApplication::onParseCommandLine` @ 0x180106790,
`MOXIE::Application::onParseCommandLine` @ 0x18001c4e0, `LApplication::onInitialize`,
`RenderManager::captureStart/enableHqRendering`, `SGReplay::record/replay`).
Scripts: `tools/ghidra/DumpStringUsers.java`, `tools/ghidra/DecompileByName.java`.
**[F]** = read from code; **[?]** = semantics not yet traced. Nothing here has been executed yet.

Parsing rules **[F]**: arguments are matched exactly (case-sensitive), unknown arguments are
ignored. `on|off` values are compared with `_stricmp("on", …)` → anything other than `on`
means off. Optional numeric values are only consumed if the next token does not start with `-`.

`storyplayer.exe` itself only checks for the substrings `-fullscreen` and `-latlong`
(`wcsstr`); Steam's `-NOfullscreen` is therefore simply "not `-fullscreen`" → windowed.

## Story / application options (`LApplication`)

| Option | Argument | Effect **[F]** |
|---|---|---|
| `-package` | name | story package (Steam: `pearl_vrcam`) |
| `-scene` / `-segment` | name | stored, exposed to Lua **[?]** |
| `-config` | path | story config |
| `-root` / `-shared` | dir | root / shared package directories |
| `-reason` | string | launch reason |
| `-zip` | on\|off | flag **[?]** |
| `-persist` | on\|off | enable `persist.lua` handling (default on) |
| `-record` | file | camera recording (CSV, see below); **if no `-fixedtimestep` is given, a fixed step of 16.667 ms is forced** |
| `-replay` | file | replay a camera CSV |
| `-playblast` | dir | start frame capture into `dir` (`RenderManager::captureStart`) |
| `-hqoffline` | dir | high-quality offline render: `enableHqRendering(on, samples=128, 2.0, aasharpness)` then capture into `dir` |
| `-aasharpness` | float | HQ filter sharpness (default 0.5) |
| `-rendertodisk` | on\|off | flag passed to Lua as `Platform.renderToDisk`; `on` also sets the fixed step field to 1.0 ms **[?]** |
| `-supersample` | on\|off | supersampling (only when not `-hqoffline`) |
| `-timescale` | [float] | global time scale, clamped to 0.0625 … 16 (default 1.0) |
| `-fixedtimestep` | [ms] | fixed simulation step in milliseconds (default 16.6667 if no value; min 1.0); written ×1000 (µs) into the global timer |
| `-primetime` | on\|off | flag **[?]** |
| `-displaymode` | string | display mode **[?]** |
| `-checkpoint` | id | start at checkpoint (Lua `Story:start(cp)`) |
| `-generateCheckpoints` | — | `Platform.generateCheckpoints` |
| `-loadfile` | name | restore `saves/<name>.dat` (Lua `Story:restore`) |
| `-autosave` | — | autosave every 5 s |
| `-saveinterval` | [s] | autosave interval (default 15, min 5) |

## Display options (`Application`)

| Option | Argument | Effect **[F]** |
|---|---|---|
| `-res` | W H | window/render size |
| `-msaa` | N | MSAA samples (Steam: 2 → observed 2× MSAA eye targets) |
| `-ssaa` | float | supersampling scale (Steam: 1.0 → eye target = SteamVR recommended size) |
| `-osd` | … | on-screen display flag **[?]** |
| `-perf` | on\|off | performance stats |
| `-vsync` | on\|off | vsync |
| `-stereoscopic` | on\|off | stereo on/off (forced) |
| `-vr` | — | force stereo/VR |
| `-mono` | — | force mono (desktop, non-VR) |
| `-360` | — | 360° mode |
| `-latlong` | size [w] [h] | equirectangular output |
| `-stats`, `-debug`, `-display`, `-webeditor`, `-sdk` | | diagnostics / editor **[?]** |
| `-pkgname`, `-pkgpath`, `-storyConfig`, `-manifest`, `-dumpinfo`, `-colormips` | | packaging / debug **[?]** (`-colormips` presumably tints mip levels) |

## Capture output **[F]**

`RenderManager::captureStart(dir)` writes **TGA** files (24-bit, uncompressed, 2 764 818 bytes at
1280×720). **The directory must already exist and be given as an absolute path** — verified in
run06/run07: the engine never creates it (the create branch in the code is not effective), a
relative path produced nothing. Capture survives render-graph switches (frames continue across
all shots).
* stereo/VR: `dir/screenshot_eye<E>_<NNNNN>.tga` (one file per eye per frame)
* mono: `dir/screenshot_<NNNNN>.tga`
* a pattern with `#` (Lua `renderToDisk`, default `screencapture[####].tga`) becomes
  `…_eye%d_%0Nd` / `%0Nd`.

## Camera recording format **[F]**

`SGReplay::record(file)` opens a `CSVLog` with columns **`time, pos, rot, scale, marker`**;
`replay(file)` loads the same CSV. Lua sets markers per FSM state (`Replay:setMarker`).
A recording therefore is a human-readable per-frame camera pose log — directly usable as
reference input for a new runtime.

## Reference-capture invocations

Verified (run07a): `-mono -fixedtimestep 33.3333 -playblast <existing absolute dir>` → 2374
frames in 20 s wall time covering intro → seq2 (story ≈79 s at 30 fps steps).

```
win64\storyplayer.exe -package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0 -mono -fixedtimestep 33.3333 -playblast <dir>
win64\storyplayer.exe -package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0 -record <pose.csv>
win64\storyplayer.exe -package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0 -replay <pose.csv> -fixedtimestep 33.3333 -playblast <dir>
```
