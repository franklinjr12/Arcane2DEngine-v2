set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# --- headless tests: no window, no GL, no audio device --------------------
foreach(t test_logger test_timestep test_json test_scene test_scene_json test_input test_animation test_audio test_hud test_text test_hud_input test_tilemap test_particles)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE arcane_runtime)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
target_include_directories(test_json PRIVATE ${TP}/json)

add_executable(test_editor_state test_editor_state.cpp)
target_link_libraries(test_editor_state PRIVATE arcane_editor_core)
add_test(NAME test_editor_state COMMAND test_editor_state)

add_executable(test_editor_project test_editor_project.cpp)
target_link_libraries(test_editor_project PRIVATE arcane_editor_core)
target_compile_definitions(test_editor_project PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_editor_project COMMAND test_editor_project)

add_executable(test_editor_anim_audio test_editor_anim_audio.cpp)
target_link_libraries(test_editor_anim_audio PRIVATE arcane_editor_core)
add_test(NAME test_editor_anim_audio COMMAND test_editor_anim_audio)

add_executable(test_clip_authoring test_clip_authoring.cpp)
target_link_libraries(test_clip_authoring PRIVATE arcane_editor_core)
add_test(NAME test_clip_authoring COMMAND test_clip_authoring)

add_executable(test_physics_headless test_physics_headless.cpp)
target_link_libraries(test_physics_headless PRIVATE box2d)
add_test(NAME test_physics_headless COMMAND test_physics_headless)

add_executable(test_physics_components test_physics_components.cpp)
target_link_libraries(test_physics_components PRIVATE arcane_runtime)
add_test(NAME test_physics_components COMMAND test_physics_components)

add_executable(test_platformer test_platformer.cpp)
target_link_libraries(test_platformer PRIVATE arcane_runtime)
add_test(NAME test_platformer COMMAND test_platformer)

add_executable(test_audio_null test_audio_null.cpp)
target_link_libraries(test_audio_null PRIVATE miniaudio)
add_test(NAME test_audio_null COMMAND test_audio_null)

add_executable(test_imgui_context test_imgui_context.cpp)
target_link_libraries(test_imgui_context PRIVATE imgui)
add_test(NAME test_imgui_context COMMAND test_imgui_context)

# --- module loader test, including the version-mismatch rejection path ----
add_library(bad_version_module MODULE bad_version_module.cpp)
target_link_libraries(bad_version_module PRIVATE arcane_runtime)
set_target_properties(bad_version_module PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)

add_executable(test_module_load test_module_load.cpp)
target_link_libraries(test_module_load PRIVATE arcane_runtime)
target_compile_definitions(test_module_load PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>"
  ARCANE_BAD_VERSION_MODULE_PATH="$<TARGET_FILE:bad_version_module>")
add_test(NAME test_module_load COMMAND test_module_load)

add_executable(test_module_registration test_module_registration.cpp)
target_link_libraries(test_module_registration PRIVATE arcane_runtime)
target_compile_definitions(test_module_registration PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_module_registration COMMAND test_module_registration)

# --- GL-labeled tests: need a real (possibly offscreen) GL context --------
# Under ARCANE_SANITIZE, the Mesa llvmpipe software rasterizer JIT-compiles
# shader code at runtime into pages LSan can't attribute to any loaded module
# ("<unknown module>" frames), so it reports them as leaked at process exit.
# This is driver behavior outside our code, not an application leak; disable
# leak detection (ASan error-detection stays on) for GL-labeled tests only
# when sanitizers are enabled. See docs/decisions.md.
set(ARCANE_GL_TEST_ENV "")
if(ARCANE_SANITIZE AND NOT MSVC)
  set(ARCANE_GL_TEST_ENV "ASAN_OPTIONS=detect_leaks=0")
endif()

add_executable(test_renderer_gl test_renderer_gl.cpp)
target_link_libraries(test_renderer_gl PRIVATE arcane_runtime)
add_test(NAME test_renderer_gl COMMAND test_renderer_gl)
set_tests_properties(test_renderer_gl PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
  ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

add_executable(test_tilemap_gl test_tilemap_gl.cpp)
target_link_libraries(test_tilemap_gl PRIVATE arcane_editor_core)
add_test(NAME test_tilemap_gl COMMAND test_tilemap_gl)
set_tests_properties(test_tilemap_gl PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
  ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

add_executable(test_particles_gl test_particles_gl.cpp)
target_link_libraries(test_particles_gl PRIVATE arcane_editor_core)
add_test(NAME test_particles_gl COMMAND test_particles_gl)
set_tests_properties(test_particles_gl PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
  ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

if(ARCANE_BUILD_SAMPLES)
  add_test(NAME test_player_frames COMMAND arcane_player --frames 30 --hidden)
  set_tests_properties(test_player_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

  add_test(NAME test_editor_frames COMMAND arcane_editor
    --project ${CMAKE_SOURCE_DIR}/samples/sample_project --module $<TARGET_FILE:sample_game>
    --frames 30 --hidden --play-frames 10)
  set_tests_properties(test_editor_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

  add_test(NAME test_player_project_frames COMMAND arcane_player
    --project ${CMAKE_SOURCE_DIR}/samples/sample_project --module $<TARGET_FILE:sample_game>
    --frames 30 --hidden)
  set_tests_properties(test_player_project_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
endif()

# Exports the sample game to a temp dir and runs the exported player there.
if(ARCANE_BUILD_SAMPLES AND UNIX)
  add_test(NAME test_export_game
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_export_game.sh
      ${CMAKE_SOURCE_DIR}/scripts/export-game.sh ${CMAKE_BINARY_DIR})
  set_tests_properties(test_export_game PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 300
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
endif()

add_executable(test_editor_tilemap test_editor_tilemap.cpp)
target_link_libraries(test_editor_tilemap PRIVATE arcane_editor_core)
add_test(NAME test_editor_tilemap COMMAND test_editor_tilemap)

add_executable(test_tile_paint test_tile_paint.cpp)
target_link_libraries(test_tile_paint PRIVATE arcane_editor_core)
add_test(NAME test_tile_paint COMMAND test_tile_paint)
