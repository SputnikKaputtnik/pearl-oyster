# Own runtime: first desktop viewer (milestone M1)

Status 2026-10-03. Route C (replacement player) has started; this records the architecture
decisions, what the viewer reproduces and how it was validated against the original.

## Architecture decisions

| Decision | Choice | Reason |
|---|---|---|
| Language / build | C++17, CMake + Ninja; MSYS2 gcc on Windows, Android NDK clang later | portable, no runtime dependencies |
| Graphics API | **OpenGL ES 3.0** (desktop: SDL2 ES context; Quest: EGL + OpenXR) | the original shaders are GLSL ES 1.00 and run unchanged on both |
| Story logic | original Lua on **LuaJIT 2.1** (planned; 2.0.4 has no ARM64) | Lua scripts are the spec; no hand port |
| Until Lua runs | scene of a state assembled from the def JSON exported by `tools/lua_data_dump.py` | lets rendering be validated first |
| Profiles | **Original** (default, reference behaviour) and **Remaster** (opt-in switches) | preservation rules; user wants interpolated animation at 60/72/90 Hz, higher resolution, ASW, grain toggle |

Remaster hooks already in the code: `SamplingPolicy` (ignore stepped keys / stepped VANM
meshes → interpolated animation), render size and MSAA are parameters. Planned: grain
toggle (`u_GrainAmplitude` = 0), ASW motion vectors from consecutive vertex-animation frames,
camera-cut detection so interpolation never blends across a cut.

## LuaJIT

Official source (`git clone --branch v2.1 https://luajit.org/git/luajit.git`, checked out at
`C:\Tools\luajit`, commit c6ffc141, 2026-09-08; not part of this repo, MIT licence). Built by
`runtime/cmake/luajit.cmake`, which mirrors `src/msvcbuild.bat` with gcc (minilua → DynASM
`vm_x64.dasc` → buildvm → `lj_vm.o` + generated headers, all in the build directory); defaults
kept (GC64, no Lua 5.2 compat — the original used 5.1-dialect LuaJIT 2.0.4). Smoke test
`oyster_lua`: JIT on, FFI on; **all 179 shipped `.lua` files compile** with it.

## Code map (`runtime/`)

* `src/core` – binary/bit readers, FNV-1a, package FS (case-insensitive URIs), JSON, math
  (row-major matrices, column vectors, uploaded untransposed because shaders compute `v * M`).
* `src/assets` – `.mxm` (+ compressed geometry, subdivision stencils), `.mxa` (+ VANM
  decoder), `.shd`, `.dds`, `.pfb`, material/render-state records.
* `src/scene` – animation playback and sampling (engine-exact), `ModelInstance`
  (hierarchy, transform + material + render-graph parameter animation, vertex animation).
* `src/render` – GLES3 loader, renderer (materials, engine uniforms, sort key, MSAA targets),
  render graph (scene nodes + post-effect image nodes).
* `apps/check` – loads every asset; totals equal the Python reference tools.
* `apps/viewer` – renders one state at given times to TGA (`--count/--dt` batches).

## Engine semantics implemented (from Ghidra, see engine-internals.md for addresses)

* Animation time/loop update, key search with per-channel cache, per-key step bits
  (vec3 per component, quat/float whole key), slerp with 0.99 lerp threshold, int rounding,
  bool channels never interpolate. Interpolation flag defaults to 1; Pearl never changes it.
* Material parameter hash = FNV-1a of the uniform name without `u_`; custom animation
  channels: `[materialHash, paramHash, pass, component]` (models) or, with type 1,
  `[paramHash, type, component]` applied to render-graph image materials.
* Pass selection: single-pass materials draw in every view the actor's `viewFlags` allow;
  multi-pass materials draw only pass index = view `passId` (0 warp, 1 shadow, 2 colour).
* Sort key (`RenderDispatcher::makeSortKey`): group = pass `ud4 & 0xFFFF` (≤ 255), blend bit,
  opaque: shader then front-to-back, blended: back-to-front; depth = view-space distance of the
  transformed mesh bounds centre / far + per-mesh bias (mesh field formerly `u32_120`, a float).
* Lights: view-space, direction = light +Z axis, `w` = 1 for ambient, colour = diffuse + wrap;
  ≤ 4 per draw. Normal matrix = inverse transpose of world·view.
* Camera: perspective FovY, far clamped to near × 500000; mono rig = boom origin rotation,
  position + R·(0,0,−L).
* Render graph from the rgraph def (`default` branch), view scales/clear colours from
  `def.renderviews`, image nodes with `RenderState::DEFAULT`, inputs → `u_textureN`,
  `u_texelSize[i]` = input texel size, MSAA 2× scene targets resolved with a NEAREST blit.

## Validation: S01_10_animB (car interior, night) against run07a

Frame alignment: reference frame = state frame + 825 (the state starts at run07a frame 825
because of preload pauses; predicted 805 from durations alone).

| Metric over all 463 frames (1280×720) | Value (0–255 scale) |
|---|---|
| mean absolute difference per pixel | 3.92 (max frame 5.59) |
| same after 4× downscale (structure) | 1.60 (max frame 3.78) |
| one frame after 3 px blur | 0.77, no colour bias |

The remaining per-pixel difference is dominated by **film grain**: its strength matches
(high-pass std 3.8 vs 3.6) but its per-frame phase does not, because the origin of `u_time`
(process start, load pauses) is unknown. This is not visible to a viewer and is left open.

Open differences, in order of visibility:
1. A soft diagonal light band (bloom/mask chain) is weaker than the original in some frames
   (worst around reference frames 1129–1132).
2. Dust particles (`DustAFX`, `.mxb`) are not implemented yet.
3. Grain/warp-noise phase (`u_time` origin), invisible.
4. Single-pixel edge differences on highlights.
