# Launch chain: Steam → story bootstrap → scene loading

| Step | What happens | Status | Evidence |
|---|---|---|---|
| 1 | Steam starts `win64/storyplayer.exe` (or `win32/` on 32-bit OS) with `-package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0` | **[F]** | appinfo.vdf record 476540 |
| 2 | Working directory | **[F]** root works / **[?]** what Steam sets | With cwd = install root the engine finds everything and writes `log.txt`, `persist.*`, `saves/` there (run02). The older `win64/log.txt` suggests some past launch used `win64/` as cwd; Steam's actual choice is not yet observed. |
| 3 | `storyplayer.exe` reads `GetCommandLineW` → `CommandLineToArgvW`, `_getcwd`, creates the window via `Win32Utils::createWindow`, calls `moxieLoad` → `moxieStoryPrepare` → `moxieStoryPlay`, then pumps messages and calls `moxieWindowDraw` / `moxieWindowReshape` | **[F]** imports / **[I]** order | `research/pe/win64.txt` |
| 4 | moxie.v2 parses engine options (`-package`, `-msaa`, …), mounts packages via `PackageFileSystem` with search roots `./common`, `../../../packages/common`, `../../../packages/story`; URIs are `package:path` | **[F]** strings / **[I]** flow | runtime-analysis.md |
| 5 | Creates LuaJIT state (`luaL_newstate`, `luaL_openlibs`), registers `LApi*` tables, loads `persist.lua` (`persist.chk` checksum, "Persist table was corrupted" warning) | **[F]** strings | `lapplication.cpp` messages |
| 6 | Loads **`story/scripts/app.lua`** (the generic bootstrap). `pearl_vrcam/scripts/app.lua` is never even probed | **[F]** Procmon run02 | Pearl's own app.lua (post-effect `def.filters`) is dead code; `Time.setErrorCorrectParams(true, 0.25)` is active |
| 7 | `app.lua` → `require "story/scripts/story"` (framework + common library, 79 modules) → `require "pearl_vrcam/scripts/data/package"` (14 data files + 39 render graphs) → `Story(def.story)` | **[F]** | `research/lua_require_tree.txt` |
| 8 | Native calls `Application.onInitialize()` → `Story:initialize()` → `Story:start()`; FSMs `Main` (start `start_standing`), `AudioEmitter` (`AudioTest_1_05`), `PreloadSequence` (`Preload`) start, each prefixed by a `preload` pseudo-state | **[F]** | `story.lua`, `fsm.lua`, `data/story.lua` |
| 9 | Each state's `startSegment` creates a `ScenePlayer`: instantiates the state's `active` actors via `Factory` (`Scene.createActor` etc. → native `ModelResource` loads `.mxm`, materials load `.shd`/`.dds`), adds animation clips `actor.def.animation[stateName]` (`.mxa`), plays them, prefetches all transition targets | **[F]** | `commands.lua:266–392`, `sceneplayer.lua` |
| 10 | Per frame: `Application.onUpdate` → `Story:update` (scheduler, FSM, `scene:update`); `CameraRigController:onLateUpdate` sets the camera from HMD pose + rig; `Application.onRender` → `RenderManager.draw()` (active render graph = current shot's post-effect graph) | **[F]** | `story.lua`, `camerarigcontroller.lua` |

The entry point into the Pearl content is therefore the generic **`story/scripts/app.lua`**,
which loads `pearl_vrcam/scripts/data/package.lua` through `Platform.package` (observed); the authored story itself is the `def["Main"].states` table in
`pearl_vrcam/scripts/data/states.lua`, starting at state `start_standing`.

Answered by the observed runs (see `runtime-observations.md`): cwd = root works; the generic
`story/scripts/app.lua` runs; `moxie.v1` and FreeImage are never loaded. Still open: the
working directory Steam itself sets.
