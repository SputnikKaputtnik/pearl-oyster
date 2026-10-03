# Oyster - Pearl for Meta Quest (OpenXR)

Native Quest app around the shared runtime: the original Lua story, the original shaders and the
user's own Pearl data, rendered per eye like the original's SteamVR build (docs/vr.md).

## Build

Prerequisites (shared with other projects on this machine, used read-only): Android SDK
`C:\Android\Sdk` (platform 35, NDK 27.2.12479018, CMake 3.22.1), Android Studio's JBR, the
Khronos OpenXR loader for Android in `C:\OpenXR\install\android-arm64-v8a`, the LuaJIT /
libogg / libvorbis checkouts in `C:\Tools\luajit|ogg|vorbis`, MSYS2 gcc (host compiler for
LuaJIT's build tools). `local.properties` (not committed) holds `sdk.dir=C\:\\Android\\Sdk`.

```powershell
$env:ANDROID_HOME = 'C:\Android\Sdk'
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat --no-daemon --offline :app:assembleDebug
```

Output: `app/build/outputs/apk/debug/app-debug.apk`.

## Data (the user's own installation)

The APK contains no Pearl assets. An offline installer step prepares your installation for
the headset: `oyster_prepare` (desktop build of the runtime, `apps/prepare`) copies the four
content folders (`common`, `pearl_vrcam`, `pearlpackage`, `story`) and converts every DXT5
texture - which the Quest GPU cannot sample - into an uncompressed RGBA8 DDS of the same name,
using the runtime's decoder (pixel-identical to the desktop GPU). The source is only read.
Result: about 4.6 GB, about 6 s on a desktop.

```powershell
oyster_prepare --root "<Pearl installation>" --out "<folder>"
adb shell mkdir -p /sdcard/Android/data/org.oyster.pearl/files/pearl
adb push "<folder>/." /sdcard/Android/data/org.oyster.pearl/files/pearl/
```

Unprepared data still works (textures are then decoded while loading, with a warning in the
log), but costs CPU time during the story.

Checkpoint saves are written to `/sdcard/Android/data/org.oyster.pearl/files/saves/`.

## Run

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n org.oyster.pearl/android.app.NativeActivity
adb logcat -s OysterPearl OysterEngine
```

Pearl waits for a seated viewer (camera height between 80 and 130 units, i.e. a seated head
height); the tracking origin is the floor (LOCAL_FLOOR).

## Behaviour

* Head pose and per-eye field of view from `xrLocateViews` at the predicted display time; the
  IPD is the distance of the two eye positions.
* The eye images are copied 1:1 into sRGB swapchains with sRGB write encoding disabled, so the
  compositor receives the same display-referred values the original submitted to SteamVR.
* DXT5 textures are uploaded as they are (the Adreno 740 decodes S3TC natively); without the
  extension the software decoder reproduces the desktop GPU arithmetic. Textures are kept within
  a 1 GB budget (least recently used are freed).
* Story time advances only while the app has focus; audio (AAudio, 48 kHz) pauses with it.
* Shaders are built before the story starts ("COMPILING SHADERS" panel), from a program binary
  cache after the first start.
* Two threads share the frame: the engine thread runs `xrWaitFrame`, the head pose, the story,
  animation and scene preparation and records the GL calls; a render thread owns the GL context,
  replays them (the driver work) and does `xrBeginFrame`, the swapchain copies and `xrEndFrame`
  (`src/render/gl_thread.h`). Frame N is drawn while frame N+1 is computed. The command order is
  the call order, so the images are identical to single-threaded rendering (verified on the
  desktop player with `--threaded`). The flag file `/sdcard/Oyster/singlethread` disables it.

## Settings

`/sdcard/Oyster/oyster.cfg` (written with the defaults on the first start, read at start):

```
resolution_scale = 1.30   # eye image = recommended size (1680x1760 on Quest 3) x this (default 1.3 = 2184x2288)
animation = original      # or: interpolated (remaster: the authored stepped keys interpolated)
msaa = 2                  # 1, 2 (the original's -msaa 2) or 4
eye_height_offset = 0.30  # not in the original: eye cameras raised by this many metres (0 = original)
```

While the story plays, controller button **A** (right) or **X** (left) switches the animation
between original and interpolated; a panel shows the new mode for two seconds. The thumbsticks
(up/down) raise or lower the eye cameras in 5 cm steps (saved in oyster.cfg). This is a deviation
from the original, display only: the story still sees the real head pose (seat check, camera rig).

Shot changes: textures and mesh buffers of the models the story prefetches are uploaded in the
frames before they are needed. Resources the story loads without announcing them (or announces
too late) are learned in `/sdcard/Oyster/prefetch_hints.txt` and prefetched on later runs.

## Self test (no headset needed)

`oyster_selftest` (built with the Android CMake build, see `apps/android_selftest`) runs the story
from `adb shell` offscreen at the eye size and field of view of the Quest 3 with a still, seated
head and prints a per-shot table (engine thread, render thread, GPU wait, frame period):

```bash
adb push oyster_selftest /data/local/tmp && adb shell chmod 755 /data/local/tmp/oyster_selftest
adb shell "cd /data/local/tmp && ./oyster_selftest /sdcard/Oyster/pearl 30000 72"
```

`OYSTER_SKIP_TO=<state>` fast-forwards (without rendering) to a story state,
`OYSTER_THREADED=0` measures single-threaded, `OYSTER_DEBUG_TIMING=1` adds the engine's CPU
breakdown and `OYSTER_DEBUG_GLSYNC=1` counts the GL calls that had to wait for the render thread.
