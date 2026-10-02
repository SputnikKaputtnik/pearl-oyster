# Native runtime analysis (static, read-only)

Tools: `tools/pe_inspect.py` (pefile), `tools/demangle_exports.py` (dbghelp
`UnDecorateSymbolName`), `tools/source_paths.py`, GNU `strings`. Raw outputs:
`research/pe/win32.txt`, `research/pe/win64.txt`, `research/pe/*.exports.tsv`.
String dumps are regenerable and kept outside the repo (`C:\Tools\pearl-work\strings`).

## Binaries (win64; win32 is the same set built for i386)

| File | Arch | Link time (UTC) | Notes |
|---|---|---|---|
| `storyplayer.exe` | AMD64, GUI | 2016-09-16 17:23:24 | PDB `C:\source.git\gss\bin\x64\Candidate\storyplayer.windows.pdb`; 18 KERNEL32 + 9 USER32 imports + 11 from moxie.v2 |
| `moxie.v2.shared.windows.dll` | AMD64 | 2016-09-16 17:23:22 | **6221 exports** (C++ mangled, i.e. full class/method names); main engine |
| `moxie.v1.shared.windows.dll` | AMD64 | 2016-09-15 23:49:20 | 4411 exports; not referenced by exe or v2 |
| `lua51.dll` | AMD64 | 2016-08-31 | **LuaJIT 2.0.4** (string), 135 Lua 5.1 C-API exports |
| `openvr_api.dll` | AMD64 | 2016-07-04 | Valve buildslave PDB; interfaces requested: `IVRSystem_012`, `IVRCompositor_016` (≈ OpenVR SDK 1.0.x, mid-2016) |
| `PVRTexLib.dll` | AMD64 | 2014-10-07 | v4.14.6; moxie.v2 imports only `pvrtexture::PixelType` ctor |
| `FreeImage.dll` | AMD64 | 2016-08-19 | 3.17.0; **not imported** by exe/v2 [F] (possibly v1 or tools) |
| `libcurl/libeay32/ssleay32/libssh2` | AMD64 | 2014 | network file system / analytics / RPC |
| `pthreadVC2.dll` | AMD64 | 2012 | threads |
| `msvcp140/vcruntime140/concrt140` | **i386** | 2016-07-13 | wrong bitness in `win64/` (see architecture.md) |

## Host program (`storyplayer.exe`)

Imports from moxie.v2 **[F]**: `moxieLoad`, `moxieStoryPrepare`, `moxieStoryPlay`,
`moxieStoryStop`, `moxieWindowDraw`, `moxieWindowReshape`, `Win32Utils::createWindow /
destroyWindow / wndProc / loadWindowLayout / saveWindowLayout`. Own strings: `-latlong`,
`-fullscreen`, "Failed to load Moxie DLL.", "Failed to play the story.", calls `_getcwd`,
`GetCommandLineW` + `CommandLineToArgvW`. **[I]** The exe is a thin shell: parse argv, create
window, hand argv + cwd to `moxieLoad/StoryPrepare/StoryPlay`, pump messages, call
`moxieWindowDraw` per frame.

## Graphics

* **[F]** moxie.v2 imports 45 functions from `OPENGL32.dll` (GL 1.1 core + `wgl*`), uses
  `wglGetProcAddress` and contains the complete GLEW name table (static GLEW; GLEW license in
  `licenses.txt`). Desktop OpenGL via WGL.
* **[F]** Shader backend class is `MOXIE::ShaderGL_ES2`; all 303 shipped `.shd` files contain
  **GLSL ES 1.00-style** source (`precision highp float;`, `attribute`/`varying`,
  `gl_FragColor`, `texture2D`, no `#version`). The same shader text is fed to desktop GL.
* **[F]** Render targets requested by data: RGBA8/RGB8 colour + `DEPTH24_STENCIL8`;
  MSAA/SSAA from CLI (`-msaa 2 -ssaa 1.0` in Steam's launch line).
* **[F]** Some textures are ETC2/"ETA8" DDS (film grain, paper "tooth" texture, particle
  sprites, splash). **[I]** decoded via PVRTexLib on desktop.
* **[?]** Exact GL entry points used at runtime (GLEW table lists every GL function) — needs
  an API trace (apitrace; RenderDoc cannot attach to the legacy context, see runtime-observations.md) in Phase 2.

## VR / SteamVR

* **[F]** moxie.v2 imports `VR_InitInternal, VR_ShutdownInternal, VR_IsHmdPresent,
  VR_GetGenericInterface, VR_IsInterfaceVersionValid, VR_GetInitToken,
  VR_GetVRInitErrorAsEnglishDescription` — i.e. the standard `vr::VR_Init` inline path,
  `IVRSystem_012` + `IVRCompositor_016`.
* **[F]** Lua sees VR through `DisplayDevice` (`getType` DT_HMD/DT_HHD/DT_360,
  `getSensorFusionOrientation`, `getSensorFusionTranslation`, `hasSensorRotation`,
  `isRoomBasedVR`, `setInterpupillaryDistanceScalar`, `setFovScalar`, `setSplashPath`).
* **[F]** `licenses.txt` also lists the Oculus SDK, but no LibOVR import or `ovr_` string is in
  moxie.v2 **[F]** → Oculus PC support (if any) only via SteamVR in this build **[I]**.
* Opportunity **[I]**: because OpenVR is a separate DLL with a tiny import surface, a proxy
  `openvr_api.dll` (OpenVR→OpenXR) placed into a *copy* could run the untouched original on
  any modern OpenXR runtime and doubles as an instrumentation point (pose/frame logging).

## Audio

* **[F]** `dsound.dll` loaded dynamically (string, not imported); WINMM `timeSetEvent`.
* **[F]** libtremor + libogg (licenses), Speex resampler, CIPIC HRTF database and Ircam
  credits; `common/audio/biquad_hrtf_subject_015_order_12_foa.mxhrtf` (magic `HRTF`, v2);
  ambient beds are **4-channel Vorbis (first-order ambisonics)**, dialogue mono, music stereo,
  all 48 kHz. Classes `AudioSystem`, `AudioPlayerMaux`, `AudioEmitterMaux`, `gsaDecoder`
  (`v2/src/engine/gsa/`).

## Lua

* **[F]** LuaJIT 2.0.4 (`lua51.dll`); moxie.v2 imports 58 Lua C-API functions incl.
  `luaL_loadbuffer`, `lua_setfenv`, `lua_sethook`, `luaL_register`.
* **[F]** fsm.lua comment: *"call metamethod table.__call explicitly to work around an apparent
  bug in the luajit interpreter"* → scripts were written against LuaJIT.

## Compression / archives / network

* **[F]** LZ4, libsquish, etcpack, TinyXML (`.xml.autogz` story config), base64, SHA1 listed
  in licenses. Content on disk is **loose files**, no archive container.
* **[F]** `NetworkFileSystem`, `download_cache_%p.dat`, `StoryManifest`, `%s/story.manifest`,
  `%s.gsm`, `nativeFSM:downloadState()` → the engine supports streaming stories over the
  network (Android distribution model). Steam build plays from local files **[I]**.

## Command line (moxie.v2 strings, **[F]** names / **[?]** exact semantics)

Display/quality: `-res W H`, `-fullscreen`/`-NOfullscreen`, `-msaa`, `-ssaa`, `-supersample`,
`-aasharpness`, `-vsync`, `-stereoscopic`, `-mono`, `-latlong`, `-display`, `-displaymode`,
`-colormips`. Story: `-package`, `-pkgname`, `-pkgpath`, `-root`, `-shared`, `-scene`,
`-segment`, `-config`, `-storyConfig`, `-manifest`, `-checkpoint`, `-generateCheckpoints`,
`-loadfile`, `-persist`, `-autosave`, `-saveinterval`, `-reason`. Capture/determinism:
`-record`, `-replay`, `-playblast`, `-hqoffline`, `-rendertodisk`, `-fixedtimestep`,
`-timescale`, `-primetime`. Diagnostics: `-perf`, `-stats`, `-debug`, `-dumpinfo`,
`-webeditor`, `-cap <capability>`.
Search paths: `./common`, `../../../packages/common`, `../../../packages/story`, `../raw`.

## Asset-loader related names (exports)

`ModelResource`, `AnimResource`, `VertexAnimQuantizer`, `FlipbookResource`,
`TextureResource`, `BorderPaddedTextureResource`, `ShaderResource`, `ShaderParser`,
`PipelineStateResource` (`:models/pipelineState.bin`), `ParticleMap`, `FontResource`,
`FBImageResource`, `AudResource`, `PackageFileSystem`, `NestedFileSystem`,
`InternalFileSystem`, `LocalFileSystem`, `NetworkFileSystem`, `UriManager`
(`package:path` URIs). Embedded source paths (`research/pe/moxie.v2.sourcepaths.txt`):
`v2/src/engine/animation/skeletal/animation.cpp`, `v2/src/engine/gsa/gsaDecoder.cpp`,
`v2/src/engine/lua/lapplication.cpp`, `v2/src/player/story/story_manager.cpp`, …

Next static step: load moxie.v2 into Ghidra (available at `C:\Tools\ghidra`) — the export
names make the loaders (`ModelResource::*`, `AnimResource::*`) directly navigable.
