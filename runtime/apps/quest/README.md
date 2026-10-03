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

The APK contains no Pearl assets. Copy the four content folders of your installation
(`common`, `pearl_vrcam`, `pearlpackage`, `story`, about 2 GB) into the app's external files
directory:

```powershell
adb shell mkdir -p /sdcard/Android/data/org.oyster.pearl/files/pearl
adb push "<install>\common" "<install>\pearl_vrcam" "<install>\pearlpackage" "<install>\story" /sdcard/Android/data/org.oyster.pearl/files/pearl/
```

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
* DXT5 textures are decoded on the CPU (the Quest GPU has no S3TC) with the desktop GPU's
  arithmetic; decoded textures are kept within a 1 GB budget (least recently used are freed).
* Story time advances only while the app has focus; audio (AAudio, 48 kHz) pauses with it.
