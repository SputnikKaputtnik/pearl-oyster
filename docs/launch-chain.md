# Launch chain: Steam → story bootstrap → scene loading

| Step | What happens | Status | Evidence |
|---|---|---|---|
| 1 | Steam starts `win64/storyplayer.exe` (or `win32/` on 32-bit OS) with `-package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0` | **[F]** | appinfo.vdf record 476540 |
| 2 | Working directory | **[?]** | Steam's default is the exe dir (`win64/`), but `saves/`, `persist.lua` and an empty 2025 `log.txt` live in the install root, while `win64/` also has `log.txt` + `libmoxieclient.tmp`. Either cwd is the root, or the engine resolves `..`. Verify with a file-access trace. |
| 3 | `storyplayer.exe` reads `GetCommandLineW` → `CommandLineToArgvW`, `_getcwd`, creates the window via `Win32Utils::createWindow`, calls `moxieLoad` → `moxieStoryPrepare` → `moxieStoryPlay`, then pumps messages and calls `moxieWindowDraw` / `moxieWindowReshape` | **[F]** imports / **[I]** order | `research/pe/win64.txt` |
| 4 | moxie.v2 parses engine options (`-package`, `-msaa`, …), mounts packages via `PackageFileSystem` with search roots `./common`, `../../../packages/common`, `../../../packages/story`; URIs are `package:path` | **[F]** strings / **[I]** flow | runtime-analysis.md |
| 5 | Creates LuaJIT state (`luaL_newstate`, `luaL_openlibs`), registers `LApi*` tables, loads `persist.lua` (`persist.chk` checksum, "Persist table was corrupted" warning) | **[F]** strings | `lapplication.cpp` messages |
| 6 | Loads the app script: strings `story/scripts/app.lua` and `/scripts/app.lua` → **[I]** `<package>/scripts/app.lua` (= `pearl_vrcam/scripts/app.lua`) if present, else `story/scripts/app.lua` | **[I]** | two candidates differ only in post-effect filters + `Time.setErrorCorrectParams` |
| 7 | `app.lua` → `require "story/scripts/story"` (framework + common library, 79 modules) → `require "pearl_vrcam/scripts/data/package"` (14 data files + 39 render graphs) → `Story(def.story)` | **[F]** | `research/lua_require_tree.txt` |
| 8 | Native calls `Application.onInitialize()` → `Story:initialize()` → `Story:start()`; FSMs `Main` (start `start_standing`), `AudioEmitter` (`AudioTest_1_05`), `PreloadSequence` (`Preload`) start, each prefixed by a `preload` pseudo-state | **[F]** | `story.lua`, `fsm.lua`, `data/story.lua` |
| 9 | Each state's `startSegment` creates a `ScenePlayer`: instantiates the state's `active` actors via `Factory` (`Scene.createActor` etc. → native `ModelResource` loads `.mxm`, materials load `.shd`/`.dds`), adds animation clips `actor.def.animation[stateName]` (`.mxa`), plays them, prefetches all transition targets | **[F]** | `commands.lua:266–392`, `sceneplayer.lua` |
| 10 | Per frame: `Application.onUpdate` → `Story:update` (scheduler, FSM, `scene:update`); `CameraRigController:onLateUpdate` sets the camera from HMD pose + rig; `Application.onRender` → `RenderManager.draw()` (active render graph = current shot's post-effect graph) | **[F]** | `story.lua`, `camerarigcontroller.lua` |

The entry point into the Pearl content is therefore **`pearl_vrcam/scripts/app.lua`** (or
the generic `story/scripts/app.lua` loading `pearl_vrcam/scripts/data/package.lua` through
`Platform.package`); the authored story itself is the `def["Main"].states` table in
`pearl_vrcam/scripts/data/states.lua`, starting at state `start_standing`.

Open questions for Phase 2 (all answerable by one announced, observed run):
1. cwd and the real file-open order (Procmon-style trace).
2. Which `app.lua` is executed (log output: `Story_initialize`, state transitions are logged
   as `[old]->[new]`; the generic file sets `Time.setErrorCorrectParams`).
3. Whether `moxie.v1`/`FreeImage` are ever loaded (module list).
