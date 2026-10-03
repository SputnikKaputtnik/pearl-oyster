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

## Validation (fixed step 33.3333 ms, against run07a playblast)

* The state sequence and every transition frame match the original's record markers (run06,
  constant offset: the original spends ~10 frames loading before the story starts); player frame
  `f` corresponds to reference `screenshot_{f+9}`.
* Mean absolute difference per frame (0..255), first 1300 frames (start → S01_10_animB):
  desert start 4.3-4.9, fade/title sequence 0.00-0.05 (bit-near exact), seq1_shot05B fade-in
  0.8-2.8, S01_10_anim/animB 3.84-3.92, end of animB 4.0-4.3. The ~3.9 floor is the film grain
  (per-frame noise phase, accepted as invisible).
* Fixed on the way: animated material values were matched to pass index = type field (2) - single
  pass materials (title back plate) never got them, and the light/bloom band in animB was weaker.

## Open

* Audio (AudioManager natives are logging no-ops), particles (`.mxb`), flipbooks, video cubes.
* Remaster profile options (interpolation, resolution, ASW, grain toggle) on top of the player.
