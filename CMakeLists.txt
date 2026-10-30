cmake_minimum_required(VERSION 3.22)
project(Arcane2DEngineV2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Keep executables and shared libraries side by side so a game can run
# straight out of the build/install tree without copying files around.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${cfg} CFG_UPPER)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

if(UNIX AND NOT APPLE)
  set(CMAKE_BUILD_RPATH_USE_ORIGIN_RPATH ON)
  set(CMAKE_BUILD_RPATH "$ORIGIN")
  set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

option(ARCANE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ARCANE_SANITIZE "Build with ASan+UBSan (gcc/clang)" OFF)
option(ARCANE_BUILD_TESTS "Build CTest unit/integration tests" ON)
option(ARCANE_BUILD_SAMPLES "Build sample_game and arcane_player" ON)

if(MSVC)
  add_compile_options(/W4 /wd4251 /wd4275)  # std members in exported classes: engine + clients share one CRT
  if(ARCANE_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(ARCANE_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

if(ARCANE_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

if(ARCANE_BUILD_TESTS)
  enable_testing()
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- vendored third-party libraries -----------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
add_subdirectory(${TP}/glfw EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS OFF)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/box2d EXCLUDE_FROM_ALL)

add_library(glad SHARED ${TP}/glad/src/gl.c)
target_compile_definitions(glad PUBLIC GLAD_API_CALL_EXPORT PRIVATE GLAD_API_CALL_EXPORT_BUILD)
target_include_directories(glad PUBLIC ${TP}/glad/include)

add_library(imgui STATIC
  ${TP}/imgui/imgui.cpp
  ${TP}/imgui/imgui_draw.cpp
  ${TP}/imgui/imgui_tables.cpp
  ${TP}/imgui/imgui_widgets.cpp
  ${TP}/imgui/imgui_demo.cpp
  ${TP}/imgui/backends/imgui_impl_glfw.cpp
  ${TP}/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${TP}/imgui ${TP}/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
target_link_libraries(imgui PUBLIC glfw glad)
target_compile_options(imgui PRIVATE
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w "SHELL:-include glad/gl.h">
  $<$<CXX_COMPILER_ID:MSVC>:/FIglad/gl.h>)

add_library(miniaudio STATIC ${TP}/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC ${TP}/miniaudio)
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(miniaudio PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()
target_compile_options(miniaudio PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)

# --- engine runtime ----------------------------------------------------------
add_library(arcane_runtime SHARED
  engine/src/log.cpp
  engine/src/module.cpp
  engine/src/window.cpp
  engine/src/renderer.cpp
  engine/src/node.cpp
  engine/src/scene.cpp
  engine/src/scene_io.cpp
  engine/src/prefab.cpp
  engine/src/component_registry.cpp
  engine/src/property.cpp
  engine/src/project.cpp
  engine/src/canonical_json.cpp
  engine/src/migration.cpp
  engine/src/builtin_components.cpp
  engine/src/physics.cpp
  engine/src/platformer.cpp
  engine/src/input.cpp
  engine/src/animation.cpp
  engine/src/tween.cpp
  engine/src/atlas.cpp
  engine/src/save_game.cpp
  engine/src/scene_flow.cpp
  engine/src/audio.cpp
  engine/src/hud.cpp
  engine/src/text.cpp
  engine/src/font_data.cpp
  engine/src/tilemap.cpp
  engine/src/particles.cpp
  engine/src/debug.cpp
  engine/src/replay.cpp
  engine/src/screenshot.cpp)
target_include_directories(arcane_runtime
  PUBLIC engine/include
  PRIVATE ${TP}/stb ${TP}/json engine/src)
# stb headers define unused static helpers; keep their warnings out of our build.
target_include_directories(arcane_runtime SYSTEM PRIVATE ${TP}/stb)
target_link_libraries(arcane_runtime
  PUBLIC glfw glad
  PRIVATE ${CMAKE_DL_LIBS} box2d miniaudio)
target_compile_definitions(arcane_runtime PRIVATE ARCANE_BUILD_RUNTIME)
set_target_properties(arcane_runtime PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

# --- editor core (testable, no GUI deps) ------------------------------------
add_library(arcane_editor_core STATIC
  editor/editor_core/src/project_session.cpp
  editor/editor_core/src/asset_catalog.cpp
  editor/editor_core/src/project_runtime.cpp
  editor/editor_core/src/scene_render.cpp
  editor/editor_core/src/tile_paint.cpp
  editor/editor_core/src/audio_wiring.cpp
  editor/editor_core/src/clip_authoring.cpp)
target_include_directories(arcane_editor_core
  PUBLIC editor/editor_core/include
  PRIVATE ${TP}/json)
target_link_libraries(arcane_editor_core PUBLIC arcane_runtime)

# --- authoring command registry (no GUI deps) ---------------------------------
include(cmake/embed_schemas.cmake)
embed_schemas(${CMAKE_BINARY_DIR}/generated/embedded_schemas.cpp ${CMAKE_CURRENT_SOURCE_DIR}/schemas)
add_library(arcane_tooling STATIC
  ${CMAKE_BINARY_DIR}/generated/embedded_schemas.cpp
  tooling/src/offline.cpp
  tooling/src/cli.cpp
  tooling/src/all_commands.cpp
  tooling/src/scene_commands.cpp
  tooling/src/error.cpp
  tooling/src/context.cpp
  tooling/src/command.cpp
  tooling/src/edit_session.cpp
  tooling/src/property_commands.cpp
  tooling/src/audio_commands.cpp
  tooling/src/atlas_commands.cpp
  tooling/src/flow_commands.cpp
  tooling/src/prefab_commands.cpp
  tooling/src/bench_commands.cpp
  tooling/src/replay_commands.cpp
  tooling/src/tilemap_commands.cpp
  tooling/src/hud_commands.cpp
  tooling/src/input_commands.cpp)
target_include_directories(arcane_tooling PUBLIC tooling/include ${TP}/json)
target_link_libraries(arcane_tooling PUBLIC arcane_editor_core)

# Headless authoring CLI (docs/cli-reference.md): no GL context, window or audio device.
add_executable(arcane tools/arcane_main.cpp)
target_link_libraries(arcane PRIVATE arcane_tooling)

# Headless benchmark (docs/perf.md); a short smoke run is in CTest, the full run is manual.
add_executable(arcane_bench tools/bench_main.cpp)
target_link_libraries(arcane_bench PRIVATE arcane_tooling)

if(ARCANE_BUILD_SAMPLES)
  add_subdirectory(samples/sample_game)
  add_subdirectory(samples/platformer_slice_game)
  add_subdirectory(samples/topdown_slice_game)
  add_subdirectory(player)
  add_subdirectory(editor/app)
endif()

# Export: `cmake --install <build> --prefix <dir> --component game` puts the
# player and engine library side by side (rpath $ORIGIN on Linux, DLL next to
# the exe on Windows). scripts/export-game.sh adds the project and module.
if(ARCANE_BUILD_SAMPLES)
  install(TARGETS arcane_player arcane_runtime glfw glad
    RUNTIME DESTINATION . COMPONENT game
    LIBRARY DESTINATION . COMPONENT game)
endif()

if(ARCANE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
