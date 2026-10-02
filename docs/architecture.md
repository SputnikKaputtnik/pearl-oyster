# Architecture map (Steam build 1340090)

Legend: **[F]** directly observed fact (evidence given) · **[I]** strong inference · **[?]** unknown.

## 1. Layers

```
 Steam ──► win64/storyplayer.exe  (106 KB host: window, message loop, CLI)        [Moxie runtime]
              │ imports 11 functions
              ▼
          moxie.v2.shared.windows.dll  (2.8 MB, 6221 C++ exports, MOXIE::*)        [Moxie runtime]
              │ lua51.dll (LuaJIT 2.0.4) · OPENGL32 (+GLEW, static) · openvr_api.dll
              │ dsound.dll (dynamic) · PVRTexLib · pthreadVC2 · libcurl · WS2_32       [third party]
              ▼
          Lua:  common/scripts  (engine-side Lua library, 56 files)                 [generic runtime]
                story/scripts   (Story framework: FSM, commands, camera rig, 58)    [generic runtime]
                pearl_vrcam/scripts (app.lua + 14 data files from Spotlight Tools)  [Pearl content]
                pearlpackage/rendergraphs (39 per-shot post-effect graphs)          [Pearl content]
              ▼
          Assets: pearl_vrcam (models, anims, textures, audio)                      [Pearl content]
                  pearlpackage (Pearl shaders, particles, shared textures)          [Pearl content]
                  common (engine shaders, fonts, HRTF, GUI textures)                [generic runtime]
```

| Layer | Files | Size | Evidence |
|---|---|---|---|
| Moxie runtime (generic) | `win{32,64}/storyplayer.exe`, `moxie.v1.shared.windows.dll`, `moxie.v2.shared.windows.dll` | 10.6 MB | PDB paths `C:\source.git\gss\bin\x64\Candidate\*.pdb` **[F]** |
| Third-party middleware | `lua51.dll` (LuaJIT 2.0.4), `openvr_api.dll`, FreeImage 3.17.0, curl 7.39.0, OpenSSL 1.0.1g, libssh2 1.4.3, pthreads-win32 2.9.1, PVRTexLib 4.14.6, MSVC 2015 CRT | 24.6 MB | PE version resources, `licenses.txt` **[F]** |
| SteamVR integration | `openvr_api.dll` (2016-07-04 build, interfaces `IVRSystem_012`, `IVRCompositor_016`) used only by `moxie.v2` | — | imports + strings **[F]** |
| Engine-common content | `common/` (scripts, 43 shaders, fonts, HRTF, GUI/test textures) | 33 MB | package `common` **[F]** |
| Story framework | `story/` (58 Lua, `camerarig.mxm/.mxa`) | 0.3 MB | package `story` **[F]** |
| Pearl content | `pearl_vrcam/` (739 mxm, 952 mxa, 2219 dds, 19 ogg, 14 data Lua) + `pearlpackage/` (260 shaders, 10 particle systems, 39 render graphs) | 2.07 GB | **[F]** |
| Steam redist | `_CommonRedist/vcredist/2012,2013,2015` | 57 MB | **[F]** |
| User state | `saves/`, `persist.*`, `log.txt`, `*.tmp` | 6.5 MB | see `preservation.md` |

Notes:
* **[F]** `win64/` ships the **32-bit (i386)** `msvcp140/vcruntime140/concrt140.dll` next to a
  64-bit `storyplayer.exe`; at runtime the process uses `C:\Windows\System32cruntime140.dll`
  / `msvcp140.dll` instead (observed).
* **[F]** Neither `storyplayer.exe` nor `moxie.v2` references `moxie.v1.shared.windows.dll`,
  and it is **not loaded at runtime** (module lists of run01/run02); same for `FreeImage.dll`.
  **[I]** v1 is a legacy engine for older stories.
* **[F]** The Windows DLL contains Android strings (`ANDROID_APK_VERSION_CODE`,
  `android.os.Build.VERSION.SDK_INT`, `libmoxie`). The Lua layer calls `Android.acquireScreen()`
  and loads `story/scripts/platform/android.lua` on Windows too. **[I]** One cross-platform
  engine codebase, with Android as a first-class target (`libmoxie.so`).

## 2. Native engine structure (from export names)

`research/pe/moxie.v2.win64.exports.tsv` (6221 symbols, demangled) shows the engine as a set of
services `MOXIE::EngineService<T>`: `AnalyticsManager, AudioManager, EnableManager, InputManager,
PhysicsManager, RenderDebug, RenderDevice, RenderManager, ResourceManager, SceneManager,
ScriptManager, SensorManager, StatManager, UriManager, VideoCubeManager, GlobalTime` **[F]**.

Other subsystems by symbol count: `RenderDeviceGL` (105), `ShaderParser` (103), `ShaderGL_ES2`
(58), `ModelResource` (73), `AnimResource` (43), `VertexAnimQuantizer` (26),
`PackageFileSystem` (58), `NetworkFileSystem` (55), `StoryManifest`, `StoryManager`, `StoryGSM`,
`SGModelInstance`, `SGCameraController`, `SGParticleEmitter`, `SGRenderGraph(+Animator)`,
`AudioSystem`, `AudioPlayerMaux`, `AudioEmitterMaux`, `gsaDecoder`, and a large `duet::`
namespace (720) plus story-specific `LApi*` Lua bindings (`LApiCameraRig`,
`LApiMasterBlender`, `LApiGlowFader`, …), probably carried over from earlier Spotlight stories
**[F names / I purpose]**.

Lua C API registrations seen as strings: `LApiLog, LApiFps, LApiTime, LApiMemory, LApiSystem,
LApiDisplayDevice, LApiInput, LApiRenderDebug, LApiAudioManager, LApiSensor, LApiProfiler,
LApiFileSystem, LApiStoryFSM, LApiTrigger, LApiStoryManager, LApiRenderer, LApiRPC,
LApiRenderManager, …` **[F]**.

## 3. Pearl content architecture

Lua data in `pearl_vrcam/scripts/data/*.lua` is a generated export: *"Created on Thu Sep 15 2016
… using Spotlight Tools 1.10.20160630"* **[F]**. Evaluated in a sandbox (`tools/lua_data_dump.py`),
the resulting `def` table contains **[F]**:

| def key | count | meaning |
|---|---|---|
| `actors` | 766 | model + per-state animation clips `{mxa, firstFrame, lastFrame, loop}` + `viewFlags` |
| `Main.states` | 68 | the story state machine (shots) |
| `PreloadSequence.states` | 5 | asset preloading FSM |
| `AudioEmitter.states` | 1 | positional audio FSM |
| `audioclips` / `audioemitters` | 19 / 4 | OGG clips (start/stop ms, loop, precache, `surround`) |
| `lights` | 51 | directional/ambient lights per shot (Maya names) |
| `cameras` | 2 | `MainCameraRig` (moxiecamera.mxm + per-state camera animation clips) |
| `cameraplatforms` | 1 | mono/stereo camera parameters (focal 50, near 0.1, far 10000, eye 0.065) |
| `triggers` | 11 | `LookAtTrigger` spheres (position, radius) |
| `particles` | 10 | `.mxb` particle systems |
| `rgraph` / `renderviews` | 40 / 4 | per-shot post-effect render graphs (animated via .mxa) |

Naming: actor names are Maya namespaces, e.g.
`sq01s60light:sq01s60anim:SaraKidRig:SaraKidProdModel:saraKM` → model
`pearl_vrcam:models/sq01s60light_…_sarakm.mxm` (a separate model instance per shot).
Characters per shot are listed in `pearl_vrcam/modelMapping.txt` (15 entries) **[F]**.
Units are Maya centimetres **[I]** (trigger radii ~30–310, camera height ~100).

Build provenance (`*/.manifest`) **[F]**: each output asset is recorded with the source it was
built from and a Unix timestamp — FBX → `.mxm`/`.mxa`, PNG/TGA/PSD/JPG → `.dds`, WAV → `.ogg`,
`.msh` → `.shd`, `.mxp` → `.mxb`, `.pfx` → `.pfb`. Content build: 2016-09-07…15.

## 4. How the Lua layer drives the story

Bootstrap **[F]**: the engine runs the generic `story/scripts/app.lua` (observed with Procmon;
`pearl_vrcam/scripts/app.lua` exists but is never loaded — see `runtime-observations.md`):

1. `require "story/scripts/story"` → `story/scripts/core` → `common/scripts/core` + the whole
   framework (`fsm, scheduler, commands, conditions, sceneplayer, camerarigcontroller, …`).
   Full tree: `research/lua_require_tree.txt` (132 modules, 144 edges).
2. `require(Platform.package.."/scripts/data/package")` loads all data files and the 39 render graphs.
3. `Global.story = Story(def.story)`; native host calls `Application.onInitialize/onUpdate/onRender/
   onReshape/onTerminate` **[F: strings `onInitialize`, `onRender`, … in moxie.v2; Lua side defines them]**.
4. `Story:initialize()`: Scene → Factory → Scheduler → EventDispatcher → render graphs
   (`defaultrg`, main camera, `MainCameraRig`) → display mode (IPD/FOV scalars, splash) →
   atmospherics → audio (surround emitter, HRTF) → state machines.
5. `Story:start()`; each frame `Story:update()` runs the coroutine scheduler (`fsmUpdateTask`),
   `scene:update()`, debug text; `Story:render()` → `RenderManager.draw()`.

State machine semantics (`story/scripts/fsm.lua`) **[F]**:
* A state = `{active = {objects…}, actions = {…}, transitions = {…}, duration, looping, loopcount}`.
* On entry a *preload* pseudo-state prefetches the destination's resources, then the
  `startSegment` command creates a `ScenePlayer` that instantiates every `active` object and
  plays `actor.def.animation[<stateName>]` (animation clips are keyed **by state name**).
  `state.duration` is then overwritten by the longest clip duration (`sceneplayer:maxDuration()`).
* State time advances by `Time.dt()` (frame-synchronous global clock; `Timer:step`). Timed
  actions fire when `lasttime <= t < localtime`; local events `begin/end/begin_loop/end_loop/done`.
* Transitions fire on `done` / `end_loop`, optionally guarded by a condition. Branching in Pearl
  uses only two conditions **[F]**: `lookingAt(trigger)` (gaze) and `userIsSeated(80,130,30,45,45)`.
* Commands used by Pearl's Main FSM **[F]**: `startSegment` 68, `checkpoint` 37, `datapoint` 36,
  `syncToSound` 36, `playSound` 34, `activateRenderGraph` 47, `setVolume`/`stopSound` 15,
  `actorParentToCamera` 2, `scaleActorToAspect` 2, `cameraCalibrate` 2,
  `cameraSetProjectiveOffset` 2, `cameraSetYawLock` 1, `exit` 1.
* **`syncToSound` is a no-op in this build** (body commented out, `commands.lua:498–507`) **[F]**.
  Audio and animation therefore run independently on the same global clock; there is no
  audio-driven resync. `Time.setErrorCorrectParams(true, 0.25)` (generic `app.lua`) **is active**
  in the shipped configuration **[F]**; its native meaning is **[?]**.

Story graph (Main FSM, 68 states) **[F]**: intro `start_standing → you_sat_down → you_are_sitting
→ you_are_sitting2 → fade_out → seq1_calibrate → …` through `seq1_*` … `seq6_*` to
`EndingCreditTemp`. All durations are integer multiples of 1/30 s (authoring cadence **30 fps**);
the default (no-gaze) path sums to ≈ **352 s**. Gaze branch points are short looping "wait"
states (`seq1_shotWait`, `310_Frame`, `seq4_shot10_Wait2`, `seq5_shot90_Wait`, `seq6_shot30_Hold`:
2 frames × up to 60 loops = 4 s windows) and `Seq5_shot70_Part1` (flashback). Each has two
destinations (`…Part2` vs `…Part22`, `S01_10_anim` vs `S01_10_anim2`, …). Unvisited on the
default path: `S01_10_anim2, Seq5_shot70_Part2, seq3_shot10_all_part22, seq4_shot10_Part2,
seq5_shot90_Part2, seq6_shot30_Play2, you_are_standing, you_stood_up`.

## 5. Camera / head-tracking path

`story/scripts/components/camerarigcontroller.lua` (2200 lines) **[F]**:
`onLateUpdate()` → `computeOrientation()` (sensor quat from
`DisplayDevice.getSensorFusionOrientation()` + desired yaw/pitch offsets) → behaviours/limits →
`computePosition()` = Maya-animated rig bone (`object:getBoneWorld(boomOriginBone)`) + HMD neck
vector (`DisplayDevice.getSensorFusionTranslation()`) + calibration offset → `camera:setRotation/
setPosition`. `-replay` suppresses sensor input and drives the camera from a recording.
**[?]** Whether the native side re-applies a later HMD pose (late latching) before the
per-eye render is unknown; this matters for "head tracking at display rate".

## 6. Dependency summary

* Story logic, scene composition, timing, triggers: **Lua (portable, readable, shipped as source)**.
* Rendering, resource loading, animation evaluation, audio mixing/HRTF, particles, file system,
  VR display: **native Moxie (closed binary)**, reached via ~20 Lua API tables
  (`Scene, RenderManager, Renderer, AudioManager, DisplayDevice, Sensor, Input, Time, System,
  FileSystem, StoryManager, Analytics, …`).
