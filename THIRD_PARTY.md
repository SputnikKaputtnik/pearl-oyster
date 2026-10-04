# Third-party components

Built into the Quest app (`oyster-pearl-*.apk`) and the desktop tools:

| Component | Use | License | Text |
|---|---|---|---|
| [LuaJIT](https://luajit.org/) 2.1 (Mike Pall) | runs the original Lua story | MIT | [licenses/LuaJIT.txt](licenses/LuaJIT.txt) |
| [libogg](https://xiph.org/ogg/) 1.3.6 (Xiph.Org) | Ogg container | BSD-3-Clause | [licenses/libogg.txt](licenses/libogg.txt) |
| [libvorbis](https://xiph.org/vorbis/) 1.3.7 (Xiph.Org) | Vorbis audio decoding | BSD-3-Clause | [licenses/libvorbis.txt](licenses/libvorbis.txt) |
| [OpenXR loader](https://github.com/KhronosGroup/OpenXR-SDK-Source) (Khronos Group) | `libopenxr_loader.so` in the APK | Apache-2.0 | [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| `android_native_app_glue` (Android Open Source Project, NDK) | native activity glue | Apache-2.0 | [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| libc++ (LLVM, NDK, static) | C++ standard library | Apache-2.0 WITH LLVM-exception | [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |

In the repository only: the Gradle wrapper (`runtime/apps/quest/gradle/wrapper/gradle-wrapper.jar`,
Apache-2.0). Desktop builds additionally link SDL2 (zlib license).

Not included anywhere: *Pearl* itself (Google Spotlight Stories) - story scripts, models,
animation, textures, shaders and sound come from the user's own installation at run time.
