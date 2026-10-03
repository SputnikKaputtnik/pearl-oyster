cmake_minimum_required(VERSION 3.22)
project(OysterPearlQuest LANGUAGES C CXX ASM)

if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "the Quest app builds for Android arm64-v8a only")
endif()

set(OYSTER_OPENXR_ANDROID_ROOT "C:/OpenXR/install/android-arm64-v8a" CACHE PATH "Android OpenXR loader prefix")
set(_xr_headers "${OYSTER_OPENXR_ANDROID_ROOT}/prefab/modules/headers/include")
set(_xr_loader "${OYSTER_OPENXR_ANDROID_ROOT}/jni/${ANDROID_ABI}/libopenxr_loader.so")
if(NOT EXISTS "${_xr_headers}/openxr/openxr.h" OR NOT EXISTS "${_xr_loader}")
  message(FATAL_ERROR "OpenXR loader/headers missing under ${OYSTER_OPENXR_ANDROID_ROOT}")
endif()

# the shared runtime (core, renderer, story engine, LuaJIT, Vorbis)
get_filename_component(OYSTER_RUNTIME "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
add_subdirectory("${OYSTER_RUNTIME}" oyster)

add_library(android_native_app_glue STATIC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(android_native_app_glue PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")

add_library(openxr_loader SHARED IMPORTED GLOBAL)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION "${_xr_loader}"
                                               INTERFACE_INCLUDE_DIRECTORIES "${_xr_headers}")

add_library(oysterpearl SHARED quest_main.cpp)
target_compile_definitions(oysterpearl PRIVATE XR_USE_PLATFORM_ANDROID=1 XR_USE_GRAPHICS_API_OPENGL_ES=1)
target_link_libraries(oysterpearl PRIVATE oyster_story android_native_app_glue openxr_loader android EGL GLESv3
                                          aaudio log dl)
# NativeActivity looks up ANativeActivity_onCreate in the glue archive: keep it
target_link_options(oysterpearl PRIVATE -u ANativeActivity_onCreate)
