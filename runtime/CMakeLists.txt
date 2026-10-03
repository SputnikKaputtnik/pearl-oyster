cmake_minimum_required(VERSION 3.16)
project(oyster C CXX)

# Project Oyster runtime: a Moxie-compatible player for the user's own Pearl installation.
# Core (formats, scene, GLES3 renderer) is platform independent; platform layers live in apps/.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Strict IEEE float semantics: the original engine computes in single precision without
  # fast-math; keep evaluation order and rounding comparable.
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off)
endif()

add_library(oyster_core STATIC
  src/core/pkgfs.cpp
  src/core/json.cpp
  src/assets/common.cpp
  src/assets/model.cpp
  src/assets/geometry.cpp
  src/assets/anim.cpp
  src/assets/shader_file.cpp
  src/assets/dds.cpp
  src/scene/animation.cpp
  src/scene/model_instance.cpp
)
target_include_directories(oyster_core PUBLIC src)

include(cmake/luajit.cmake)

add_executable(oyster_lua apps/lua/main.cpp)
target_link_libraries(oyster_lua PRIVATE luajit)

add_executable(oyster_check apps/check/main.cpp)
target_link_libraries(oyster_check PRIVATE oyster_core)

find_package(SDL2 CONFIG)
if(SDL2_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/renderer.cpp)
  add_library(oyster_render STATIC
    src/render/gl.cpp
    src/render/renderer.cpp
    src/render/render_graph.cpp
  )
  target_link_libraries(oyster_render PUBLIC oyster_core)
  add_executable(oyster_viewer apps/viewer/main.cpp)
  target_link_libraries(oyster_viewer PRIVATE oyster_render SDL2::SDL2main SDL2::SDL2)

  # Story engine: the original Lua scripts drive the scene (native API layer, docs/native-api.md)
  add_library(oyster_story STATIC
    src/story/lua_host.cpp
    src/story/native_api_names.cpp
    src/story/lua_json.cpp
    src/story/engine.cpp
    src/story/bind_core.cpp
    src/story/bind_scene.cpp
    src/story/bind_render.cpp
  )
  target_link_libraries(oyster_story PUBLIC oyster_render luajit)
  add_executable(oyster_player apps/player/main.cpp)
  target_link_libraries(oyster_player PRIVATE oyster_story SDL2::SDL2main SDL2::SDL2)
endif()
