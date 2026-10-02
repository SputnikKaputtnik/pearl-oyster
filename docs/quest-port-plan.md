# Quest port: route assessment and Phase 2 plan

Status: end of Phase 1 (forensics). **No route is chosen yet**; this records the evidence
and the experiments that will decide.

## What the evidence says about portability

Favourable **[F]**:
* Story logic, scene composition, timing tables, triggers and camera behaviour are **Lua
  source** (LuaJIT 2.0.4 dialect) and run unmodified on any Lua 5.1/LuaJIT, ARM64 included.
* All 303 shaders are **GLSL ES 1.00 text** compiled by a class called `ShaderGL_ES2` — they
  can be fed to GLES 3 on Quest unchanged.
* Content is loose files with simple packed little-endian containers; textures are DDS
  (DXT5/ETC2), audio is Ogg Vorbis.
* Character animation is **baked vertex animation** at 30 fps (`.mxa` kind 1) — no rig
  evaluation to replicate.
* The engine was built cross-platform with Android as a target (Android strings and Lua
  platform hooks inside the Windows build).
* Rich C++ export names (6221) make Ghidra work on the original much cheaper than usual.

Hard parts **[I]**:
* Native semantics behind ~20 Lua API tables: render graph / post-effect chain, material
  pass system (`warp → shadow → color` passes with film grain and paper texture), light
  handling, view flags, shadow-light render views.
* Particle simulation (`.mxb` with turbulence/force fields) — exact visual match needs the
  original algorithm and RNG.
* Spatial audio: 4-channel FOA beds + HRTF biquads (`.mxhrtf`) + emitters.
* Camera stereo setup (IPD scalar 83, neck model, calibration) and the exact frame loop
  (`Time.dt`, error correction, preload/buffering pauses).
* DXT5 is not hardware-supported on Quest (Adreno): needs lossless-as-possible transcoding or
  GPU decode — a *technical* conversion that must be documented and reversible.

## Candidate routes

| | A. Reuse/shim an original Android Moxie runtime | B. Port parts of the original runtime | C. Minimal Moxie-compatible replacement player |
|---|---|---|---|
| Idea | Run Google's ARM `libmoxie.so` from an old APK; shim its VR layer (Daydream GVR / Oculus VrApi) to OpenXR | Decompile and re-implement selected native subsystems 1:1 | New ARM64 OpenXR + GLES player that runs the **original Lua** and **original shaders**, with re-implemented native API + loaders |
| Fidelity | Highest, if it runs | High per subsystem | Depends on how precisely the native API is matched; measurable against references |
| Blockers | Need the binary (Oculus Go is unverified); likely **32-bit armeabi-v7a** (2016–18) — Quest 3 support for 32-bit apps is **[?]**, reportedly absent; closed blob with unknown VR/platform deps (Play services, GVR); story package format of that build may differ; redistribution impossible (user-supplied only) | No source; decompiling a 6000-export engine wholesale is large; legal grey zone if copied verbatim | Large but incremental; every subsystem verifiable against the Windows oracle |
| Reuse for PC/future XR | Low | Medium | **High** (OpenXR + GLES/Vulkan, portable) |

Additional low-cost PC track (not a Quest route, but preservation value) **[I]**: a proxy
`openvr_api.dll` translating OpenVR 1.0.x → OpenXR would let the **untouched** Windows build
run on current PC runtimes (and Quest via Link) and is the instrumentation point for
reference capture anyway.

Current leaning (to be confirmed, not decided): **C**, using the Windows build as the
reference oracle, with **A** as an accelerator if the Oculus Go yields an ARM64 or usable
`libmoxie.so` (its loaders could serve as a second oracle even if it cannot ship).

## Phase 2 plan (concrete)

Each run of the original is announced with launch count and duration before it starts.

1. **Runtime observation of the original (1 short announced run, working copy)**
   - module list (is `moxie.v1`/FreeImage loaded?), cwd, file-open order (Procmon or ETW),
     which `app.lua` runs, log output.
   - one RenderDoc (OpenGL) frame capture in a characteristic shot → exact draw calls,
     render targets, MSAA, post chain, uniform values. This becomes the render-semantics spec.
2. **CLI semantics via Ghidra** (`C:\Tools\ghidra`, moxie.v2 with export names):
   argument syntax for `-record/-replay/-fixedtimestep/-rendertodisk/-loadfile/-scene/-segment`.
   Then an announced test of a fixed-timestep render of one shot.
3. **Format work (tools in `tools/`, outputs to `C:\Tools\pearl-work`)**
   - `.mxm` parser: materials/passes, meshes, vertex layout, indices, transform hierarchy;
     validated against vertex buffers captured with RenderDoc.
   - `.mxa` kind 2 (transform tracks) then kind 1 (vertex animation dequantization);
     validated by rendering a frame offline and diffing against the reference capture.
   - `.pfb`/material render-state block, `.mxb` structure, `.fnt`, `.mxhrtf`.
   - Exit criterion: a desktop viewer that loads one shot and reproduces a reference frame
     within a defined tolerance (mono, fixed time).
4. **Native API contract**: enumerate every native table/function the shipped Lua calls
   (static scan done in part; complete it with a runtime hook on `luaL_register`/
   `lua_setfield` via a proxy `lua51.dll` in the working copy, or from Ghidra). This list is
   the specification for route C.
5. **Timing questions answered by measurement**: stepped vs interpolated 30 fps animation;
   audio/animation drift behaviour (no `syncToSound`); preload/buffering pauses.
6. **Oculus Go inventory** (read-only, only after explicit go-ahead): installed packages,
   APK(s), native libs (ABI!), cached story data. Decide A's feasibility.
7. **Decision gate**: choose route with evidence from 1–6; draft the Phase 3 architecture
   (OpenXR, GLES 3, reusable core + Quest/PC platform layers).

Display-quality improvements allowed by the project brief (higher render resolution, better
AA, filtering) only become relevant once a faithful baseline exists; each will be an explicit,
switchable option documented against the reference.
