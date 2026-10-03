# Engine internals (static analysis, Ghidra)

Source: Ghidra 12.1.3 project of `moxie.v2.shared.windows.dll` (Steam build 1340090), scripts in
`tools/ghidra/`. Addresses are virtual addresses (image base 0x180000000).
**[F]** = read from decompiled code; **[I]** = inference.

## Time model (`MOXIE::Time::tick` @ 0x1800790a0, `GlobalTime::onUpdate` @ 0x180004e20)

* `GlobalTime` ticks every registered `Time` once per update. Times are kept in
  **microseconds** (QueryPerformanceCounter → ns → /1000) **[F]**.
* Running state: `dt = (now − last) × timescale` (`+0x40` = timescale) **[F]**.
* Error correction (`setErrorCorrectParams(enable, rate)`; Pearl's bootstrap uses
  `true, 0.25`): a pending error time (`+0x58`) is fed into `dt` proportionally to the real
  frame delta × rate (`+0x60`) until consumed **[F]**. **[I]** This is how the story clock
  catches up/slows after hitches instead of jumping.
* Fixed step (`+0x48` ≠ 0, set from `-fixedtimestep` ms × 1000): `dt = fixed step`,
  independent of the real clock — no waiting/pacing loop exists in the time code **[F]**.
* `step()` (single-step from paused) injects `dt = 0x411a` µs = 16 666 µs (1/60 s) **[F]**.

## Character animation playback (`VertexAnimator::update` @ 0x1800a7e10)

* Frame position `f = elapsed × fps + start`, clamped to the last frame; `fps` is the **integer
  from the .mxa header** (`AnimResource+0x2c`, 30), set in `Animator::addAnimation`; clip
  duration = `(end − start + 1) / fps`. The float fps inside the VANM block (sometimes
  29.999998) is not used for playback → no drift from it **[F]**.
* (formula detail:)
  `f0 = floor(f)`, `f1 = f0 + 1` (wraps to 0 for looping clips), `t = f − f0` **[F]**.
* Per animated mesh a flag (VMesh field `u32` = 3 or 1, bit 1) selects the mode **[F]**:
  * **bit set (value 3, 3203 of 3473 meshes): stepped** — frame `f0` is copied, no
    in-betweens. Characters therefore move strictly on the 30 fps animation frames regardless
    of display rate — a deliberate stylistic property that a faithful port must keep.
  * bit clear (value 1, 270 meshes): linear blend `(f1 − f0)·t + f0` of positions and of the
    second channel (no renormalization).
* Per-frame visibility bits (u16 per 16-frame block) hide meshes (e.g. clothing variants).
* Decoding is incremental: inside a 16-frame block, frames are decoded forward from the block's
  absolute first frame; jumping elsewhere re-decodes from the block start.

## Frame capture pipeline

1. `RenderManager::captureStart(dir, …)` (0x18013a8b0) builds the filename pattern
   (`%s/screenshot_eye%%d_%%05d.tga` stereo, `%s/screenshot_%%05d.tga` mono,
   `%s/screenshot.tga` single, or a `[###]` pattern) and calls `FUN_1801374b0(activeGraph,
   pattern, startFrame)`, which stores the pattern in the graph (`+0x188`) and sets the
   capture flags (`+0x26c` = 0x0101) **[F]**. `setActiveRenderGraph(RenderGraph*)` re-applies
   this to new graphs **[F]** (and run07a shows capture continuing across shots).
2. Per frame, `FUN_180138650(graph, texture, eye)` formats the file name by display type —
   1 = stereo (eye + counter), 2 = cube faces (`_%06d_%s%s`, counter/7, 7 face suffixes),
   3 = special (writes only for eye 0, counter − 1), else mono — then calls
   `RenderDevice::captureScreenshot(name, texture, async=false)` and increments the global
   counter `DAT_18051d4c4` **per call** (hence `eye0_00000`, `eye1_00001`) **[F]**.
3. `RenderDeviceGL::captureScreenshot` (0x180198470) attaches the texture to a temporary FBO,
   `glReadPixels(…, GL_BGR, GL_UNSIGNED_BYTE)` (rows padded to multiples of 4), wraps the data in
   a `ScreenshotJob` and runs it immediately (sync) or queues it to a lazily created worker
   thread (async); the job writes via `Surface::save` (TGA) **[F]**.
4. The engine never creates the target directory in practice (verified run06/07); pass an
   existing absolute path.

## VR frame loop (internal OpenVR display class, not exported)

* Interface acquisition: `FUN_18018cb90` / `FUN_18018ccb0` (`IVRSystem_012`),
  `FUN_18018cc20` (`IVRCompositor_016`), init error `"Unable to init VR runtime: %s"` in
  `FUN_18018cd60` **[F]**.
* Per frame `FUN_18018d070`: drain `IVRSystem` events in a loop (vtable +0xE0, i.e.
  `PollNextEvent`) **[I: slot]**, then `IVRCompositor` vtable +0x10 (= `WaitGetPoses`) into a
  pose buffer, convert the HMD matrix to a quaternion and scale the translation by a
  display-device factor (`+0x24`) **[F/I]**.
* The VR splash / loading view uses a shader with `u_minAlpha`/`u_maxAlpha`
  (`FUN_18018d420`, `FUN_18018d640`, `RenderDeviceGL::createResources`) — matches the first
  frames seen in the apitrace (run04) **[F]**.
* `FUN_18018ca50` is a secant-method solver inverting a radial polynomial
  (`r·(1 + k1 r² + k2 r⁴ …)`) — lens-distortion style math; it loops until convergence
  **[F]**. Whether it is involved in the run08 spin is unknown.

## Open: run08 spin (VR + fixedtimestep + playblast + record)

Not explained statically: the time code has no wait loop, the capture is synchronous, the VR
loop's only `while` is the event drain. Next step proposed: sample the instruction pointers of
the spinning threads during a reproduction (Win32 `SuspendThread`/`GetThreadContext` from a
small Python tool) and map them to functions in this Ghidra project.

## Subdivision surfaces = baked stencils (`SubdivSurfaceInstance` vtable +8 @ 0x1801b7fb0)

For cages with attribute flags 3 (position + normal) the per-frame evaluation is
`FUN_1801b6db0` + `FUN_1801b7160` **[F]**:
* For each output point j (count = topology header [0]): `k = faceBytes[j]` (u8), then
  `out_j = Σ_{i<k} w_i · cage[idx_i]` for position and the second channel, with `idx` from the
  `u32[nIdx]` array and `w` from the `f32[nIdx]` array of the topology block. No recursive
  refinement at runtime — the refinement was precomputed into linear stencils.
* `FUN_1801b7160` then gathers the outputs into render-vertex order using the first index array
  of the last block (`n0 × u32[n1]`). Other attribute sets would use `FUN_1801b69d0`, but all
  516 subdivision meshes in Pearl have attribute flags 0x3, so that path is unused **[F]**.
This makes the character pipeline: decode VANM cage frame → apply stencils → remap → render.
