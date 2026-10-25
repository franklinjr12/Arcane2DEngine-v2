set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# --- headless tests: no window, no GL, no audio device --------------------
foreach(t test_logger test_timestep test_json test_scene test_scene_json)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE arcane_runtime)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
target_include_directories(test_json PRIVATE ${TP}/json)

add_executable(test_physics_headless test_physics_headless.cpp)
target_link_libraries(test_physics_headless PRIVATE box2d)
add_test(NAME test_physics_headless COMMAND test_physics_headless)

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

if(ARCANE_BUILD_SAMPLES)
  add_test(NAME test_player_frames COMMAND arcane_player --frames 30 --hidden)
  set_tests_properties(test_player_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
endif()
