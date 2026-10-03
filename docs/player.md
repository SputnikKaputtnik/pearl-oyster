# oyster_player: the original story on the Oyster engine layer

`oyster_player` runs Pearl's **original Lua story scripts** (`story/scripts/app.lua` and the
`pearl_vrcam` data) from the user's installation. The scripts build the scene, run the three
state machines and drive every animation themselves; the player provides the native API of
moxie.v2 (see [native-api.md](native-api.md)) in `runtime/src/story/`.

```
oyster_player --root <install> [--frames N] [--fixed 33.3333] [--size 1280x720] [--msaa 2]
              [--out dir/f%05d.tga --dump-from F --dump-every K] [--window] [--remaster]
              [--status K] [--watch "<lua>"] [--eval "<lua>"] [--log]
```

Run it with a scratch directory as working directory: the story tries to write checkpoints to
`saves/` (relative), exactly like the original.

## Structure

| File | Role |
|---|---|
| `story/lua_host.*` | Lua state, object model of `LUtil::bindType_`/`pushNativeType_`, package searcher over the installation, fallbacks for original natives that are not implemented |
| `story/native_api_names.cpp` | generated (`tools/gen_api_names.py`) list of every native function of the original, per type |
| `story/engine.*` | scene graph (SGNode/Actor/Camera/Light/RenderGraphInstance), Animation objects, time, `Scene.update`, `RenderManager.draw` |
| `story/bind_core.cpp` | Log, Time, System, ResourceGroup, StoryManager/StoryFSM, Input, DisplayDevice, Sensor, Stat, Analytics, `__status` |
| `story/bind_scene.cpp` | Scene, Transform, Actor, Camera, Light, Animation, Atmospherics/Fog, ParticleEmitter |
| `story/bind_render.cpp` | RenderManager, RenderGraph, RenderGraphInstance, Renderer |
| `apps/player/main.cpp` | SDL window / hidden context, frame loop, TGA dump, diagnostics |

## Engine semantics reproduced (from the decompiled engine)

* **Platform table** (`LEnvMoxie`, luareg_refs.c): Windows candidate build; string options default
  to `""`. `Story:start` only runs the FSMs because `Platform.loadFile == ""` is truthy and
  restoring that save fails.
* **Natives run inside coroutines**: the story scheduler resumes tasks as Lua coroutines, so every
  native pushes onto the calling thread's stack (not the main state's).
* **Frame**: `Application.onUpdate` (scheduler, FSMs, `scene:update()`), `Application.onRender`
  (`RenderManager.draw`). `Scene.update` = SceneGraph::update: `Transform.__onUpdate` for nodes
  with an LComponent, node update (animation sampling, then time advance), `Transform.__onLateUpdate`
  (the CameraRigController sets the camera here), deferred deletes.
* **Animations** (Animator::play, DefaultAnimMixer::update): one playing clip per actor (play stops
  the others), sampled then advanced; `playing = time < duration` after the advance, so a
  non-looping clip holds the pose sampled in its last playing frame.
* **Attachments** (SGModelInstance::addAttachment): the child follows `actor world * bone model
  matrix` - Pearl's lights and dust emitters ride on animated bones.
* **Visibility** (updateModelMatrices): own flag (`setBoneVisibility`) && animated flag && parent;
  transform-track visibility only for clips without vertex animation.
* **Animated material parameters** (SGAnimator::bindCustomAnimationAttributes): channel
  `[materialHash, paramHash, type, component]`, bound to the actor's material copy through
  `Material::getParameter`, i.e. the **first pass** that has the parameter; values persist across
  clips. Render-graph channels `[paramHash, type, component]` go to the image node materials.
* **Render view clear colour**: packed to RGBA8 (`FUN_1800e9ee0`), unpacked in RenderView::clear.
* **World transforms** compose as SRT (FUN_180088e70: component-wise scale, parent*child
  quaternion, position = parent.p + parent.r*(parent.s*child.p)); bone matrices enter via
  Transform::setMatrix. Drawing uses actor world * model-space bone matrix.
* **Attached lights** (SGLightAnimator): the bone track's custom channels "color", "range",
  "angle", "wrap" drive the light (Pearl animates the light colour per shot).
* **Look-at triggers** (FUN_180102db0/-b70): ray/sphere test; the bone variant uses the bone's
  model matrix without the actor world, as the original does.
* **Mesh instances** are baked at load time (ModelResource::fixupMeshInstances).
* **Determinism / table order**: LuaJIT 2.1 is built with a patched string id (= LuaJIT 2.0
  content hash, seed 0) and without the security randomisation, so pairs() iterates like the
  original's LuaJIT 2.0.4 and two runs are bit-identical.
* **[I] a_texcoord1 fallback**: meshes without a second UV set feed uv0 to `a_texcoord1` (warp
  pass). The engine code would disable the attribute; the observed original output (frame that
  shows the warp pass directly: 4.87 -> 1.15) requires uv0 - mechanism on the driver side.
* **Mouse** (InputManager::onUpdate, Lua `Input` natives FUN_1800dd0c0..dd2c0): once per frame;
  the delta (window pixels, y down) is updated and `isMouseMoving` is true only while the left
  button is held in this and the previous frame; vectors are pushed as Vector2 tables. The story's
  `CameraRigController:getMouseQuat` turns left-drag into yaw/pitch (0.002 rad per pixel), as in
  the original's desktop mode. Keyboard input is not fed (story.lua binds F5/F9 quicksave, R
  restart, C calibrate). Headless test: `OYSTER_DEBUG_DRAG=first,dx,dy`.

## Validation (fixed step 33.3333 ms, against run07a playblast)

* The state sequence and every transition frame match the original's record markers (run06,
  constant offset: the original spends ~10 frames loading before the story starts); player frame
  `f` corresponds to reference `screenshot_{f+9}`.
* Mean absolute difference per frame (0..255), every 50th frame of all 2366 reference frames
  (start → seq1_shot60): mean 3.82, on 4x downsampled images 1.54 (film grain averages out).
  Title sequence 0.00-0.05; most shots 3.6-4.5 (grain floor); seq1_shot20 ~7 (2.7 downsampled:
  rim highlights slightly weaker, open).
* All 31 state transitions recorded by the original (run06 markers, up to seq4_shot10_Part2)
  match frame-exactly; the whole film runs to EndingCreditTemp (~6 min story time, ~1 min
  compute headless).
* Fixed on the way: animated material values were matched to pass index = type field (2) - single
  pass materials (title back plate) never got them, and the light/bloom band in animB was weaker;
  GPU mesh cache keyed by address (reused memory gave new actors old geometry).

## Open

* Particles (`.mxb`), flipbooks, video cubes (audio: see audio.md).
* Remaster profile options (interpolation, resolution, ASW, grain toggle) on top of the player.
