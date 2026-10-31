set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# --- headless tests: no window, no GL, no audio device --------------------
foreach(t test_logger test_timestep test_json test_scene test_scene_json test_canonical_json test_formats_v2 test_input test_animation test_audio test_hud test_text test_hud_input test_tilemap test_particles test_camera2d test_tween)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE arcane_runtime)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
target_include_directories(test_json PRIVATE ${TP}/json)
target_include_directories(test_canonical_json PRIVATE ${TP}/json)

add_executable(test_editor_state test_editor_state.cpp)
target_link_libraries(test_editor_state PRIVATE arcane_editor_core)
add_test(NAME test_editor_state COMMAND test_editor_state)

add_executable(test_editor_project test_editor_project.cpp)
target_link_libraries(test_editor_project PRIVATE arcane_editor_core)
target_compile_definitions(test_editor_project PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_editor_project COMMAND test_editor_project)

add_executable(test_foundation_acceptance test_foundation_acceptance.cpp)
target_link_libraries(test_foundation_acceptance PRIVATE arcane_editor_core)
target_compile_definitions(test_foundation_acceptance PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_foundation_acceptance COMMAND test_foundation_acceptance)

add_executable(test_editor_anim_audio test_editor_anim_audio.cpp)
target_link_libraries(test_editor_anim_audio PRIVATE arcane_editor_core)
add_test(NAME test_editor_anim_audio COMMAND test_editor_anim_audio)

add_executable(test_clip_authoring test_clip_authoring.cpp)
target_link_libraries(test_clip_authoring PRIVATE arcane_editor_core)
add_test(NAME test_clip_authoring COMMAND test_clip_authoring)

add_executable(test_physics_headless test_physics_headless.cpp)
target_link_libraries(test_physics_headless PRIVATE box2d)
add_test(NAME test_physics_headless COMMAND test_physics_headless)

add_executable(test_game_context test_game_context.cpp)
target_link_libraries(test_game_context PRIVATE arcane_runtime)
add_test(NAME test_game_context COMMAND test_game_context)

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
  add_test(NAME test_export_platformer_slice
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_export_game.sh
      ${CMAKE_SOURCE_DIR}/scripts/export-game.sh ${CMAKE_BINARY_DIR}
      ${CMAKE_SOURCE_DIR}/samples/platformer_slice $<TARGET_FILE:platformer_slice_game>)
  set_tests_properties(test_export_platformer_slice PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 300
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

  add_test(NAME test_export_topdown_slice
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_export_game.sh
      ${CMAKE_SOURCE_DIR}/scripts/export-game.sh ${CMAKE_BINARY_DIR}
      ${CMAKE_SOURCE_DIR}/samples/topdown_slice $<TARGET_FILE:topdown_slice_game>)
  set_tests_properties(test_export_topdown_slice PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 300
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
  add_test(NAME test_player_topdown_frames COMMAND arcane_player
    --project ${CMAKE_SOURCE_DIR}/samples/topdown_slice --module $<TARGET_FILE:topdown_slice_game>
    --frames 120 --hidden)
  set_tests_properties(test_player_topdown_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

  add_test(NAME test_player_slice_frames COMMAND arcane_player
    --project ${CMAKE_SOURCE_DIR}/samples/platformer_slice --module $<TARGET_FILE:platformer_slice_game>
    --frames 120 --hidden)
  set_tests_properties(test_player_slice_frames PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
endif()

add_executable(test_editor_tilemap test_editor_tilemap.cpp)
target_link_libraries(test_editor_tilemap PRIVATE arcane_editor_core)
add_test(NAME test_editor_tilemap COMMAND test_editor_tilemap)

add_executable(test_tile_paint test_tile_paint.cpp)
target_link_libraries(test_tile_paint PRIVATE arcane_editor_core)
add_test(NAME test_tile_paint COMMAND test_tile_paint)

# Platformer validation slice: headless Play simulation of samples/platformer_slice.
if(ARCANE_BUILD_SAMPLES)
  add_executable(test_platformer_slice test_platformer_slice.cpp)
  target_link_libraries(test_platformer_slice PRIVATE arcane_editor_core)
  target_compile_definitions(test_platformer_slice PRIVATE
    ARCANE_SLICE_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/platformer_slice"
    ARCANE_SLICE_MODULE_PATH="$<TARGET_FILE:platformer_slice_game>")
  add_test(NAME test_platformer_slice COMMAND test_platformer_slice)

  add_executable(test_topdown_slice test_topdown_slice.cpp)
  target_link_libraries(test_topdown_slice PRIVATE arcane_editor_core)
  target_compile_definitions(test_topdown_slice PRIVATE
    ARCANE_TOPDOWN_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/topdown_slice"
    ARCANE_TOPDOWN_MODULE_PATH="$<TARGET_FILE:topdown_slice_game>")
  add_test(NAME test_topdown_slice COMMAND test_topdown_slice)

  # Legacy -> canonical v2 migration of both sample projects, with their modules.
  add_executable(test_migration test_migration.cpp)
  target_link_libraries(test_migration PRIVATE arcane_editor_core)
  target_compile_definitions(test_migration PRIVATE
    ARCANE_SAMPLE_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/sample_project"
    ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>"
    ARCANE_SLICE_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/platformer_slice"
    ARCANE_SLICE_MODULE_PATH="$<TARGET_FILE:platformer_slice_game>"
    ARCANE_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
    ARCANE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
  add_test(NAME test_migration COMMAND test_migration)
endif()

# --- every public engine header must compile standalone ---------------------
file(GLOB ARCANE_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include/arcane/*.hpp)
set(header_check_sources "")
foreach(h ${ARCANE_PUBLIC_HEADERS})
  get_filename_component(hn ${h} NAME_WE)
  set(src ${CMAKE_CURRENT_BINARY_DIR}/header_check/${hn}.cpp)
  file(GENERATE OUTPUT ${src} CONTENT "#include <arcane/${hn}.hpp>\n#include <arcane/${hn}.hpp>\n")
  list(APPEND header_check_sources ${src})
endforeach()
add_library(header_check OBJECT ${header_check_sources})
target_link_libraries(header_check PRIVATE arcane_runtime)

# --- authoring command registry: typed property commands over a real module -
add_executable(test_tooling_commands test_tooling_commands.cpp)
target_link_libraries(test_tooling_commands PRIVATE arcane_tooling)
target_compile_definitions(test_tooling_commands PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_tooling_commands COMMAND test_tooling_commands)

# AF5B: editor edit path (EditSession): GUI-vs-CLI parity, undo/redo, drag coalescing, save point, direct-write lint.
add_executable(test_edit_session test_edit_session.cpp)
target_link_libraries(test_edit_session PRIVATE arcane_tooling)
target_include_directories(test_edit_session SYSTEM PRIVATE ${TP}/stb)
target_compile_definitions(test_edit_session PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>"
  ARCANE_EDITOR_SOURCE_PATH="${CMAKE_SOURCE_DIR}/editor/app/editor_main.cpp")
add_test(NAME test_edit_session COMMAND test_edit_session)

# Prefabs: save/instantiate/override/break-link commands, runtime spawn, link validation.
add_executable(test_prefab test_prefab.cpp)
target_link_libraries(test_prefab PRIVATE arcane_tooling)
target_compile_definitions(test_prefab PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>")
add_test(NAME test_prefab COMMAND test_prefab)

# --- every shipped schema validates real canonical files (samples/, tests/golden/schema_examples/) ---
add_executable(test_schema_examples test_schema_examples.cpp)
target_link_libraries(test_schema_examples PRIVATE arcane_tooling)
target_compile_definitions(test_schema_examples PRIVATE
  ARCANE_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
  ARCANE_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples"
  ARCANE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME test_schema_examples COMMAND test_schema_examples)

# --- AF4B `arcane` CLI: end-to-end on temporary projects incl. a compiled module, exit codes, docs drift ---
add_executable(test_cli test_cli.cpp)
target_link_libraries(test_cli PRIVATE arcane_tooling)
target_compile_definitions(test_cli PRIVATE
  ARCANE_SAMPLE_GAME_PATH="$<TARGET_FILE:sample_game>"
  ARCANE_SAMPLE_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/sample_project"
  ARCANE_CLI_REFERENCE_PATH="${CMAKE_SOURCE_DIR}/docs/cli-reference.md"
  ARCANE_COMMANDS_DOC_PATH="${CMAKE_SOURCE_DIR}/docs/commands.md"
  ARCANE_CLI_PATH="$<TARGET_FILE:arcane>")
add_dependencies(test_cli arcane)
add_test(NAME test_cli COMMAND test_cli)

# asset.list/import/reimport on temporary projects (failure injection, hot reload of a reimported clip).
add_executable(test_asset_commands test_asset_commands.cpp)
target_link_libraries(test_asset_commands PRIVATE arcane_tooling)
add_test(NAME test_asset_commands COMMAND test_asset_commands)

# Registered module properties of the top-down slice through the registry.
add_executable(test_topdown_commands test_topdown_commands.cpp)
target_link_libraries(test_topdown_commands PRIVATE arcane_tooling)
target_compile_definitions(test_topdown_commands PRIVATE
  ARCANE_TOPDOWN_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/topdown_slice"
  ARCANE_TOPDOWN_MODULE_PATH="$<TARGET_FILE:topdown_slice_game>")
add_dependencies(test_topdown_commands topdown_slice_game)
add_test(NAME test_topdown_commands COMMAND test_topdown_commands)

# --- sprite atlas packing, atlas.* commands -----------------------------------
add_executable(test_atlas test_atlas.cpp)
target_link_libraries(test_atlas PRIVATE arcane_tooling)
target_include_directories(test_atlas SYSTEM PRIVATE ${TP}/stb)
add_test(NAME test_atlas COMMAND test_atlas)

# --- tilemap.* / tileset.* commands ---------------------------------------------
add_executable(test_tilemap_commands test_tilemap_commands.cpp)
target_link_libraries(test_tilemap_commands PRIVATE arcane_tooling)
target_include_directories(test_tilemap_commands SYSTEM PRIVATE ${TP}/stb)
target_compile_definitions(test_tilemap_commands PRIVATE
  ARCANE_TOPDOWN_PROJECT_DIR="${CMAKE_SOURCE_DIR}/samples/topdown_slice"
  ARCANE_TOPDOWN_MODULE_PATH="$<TARGET_FILE:topdown_slice_game>")
add_dependencies(test_tilemap_commands topdown_slice_game)
add_test(NAME test_tilemap_commands COMMAND test_tilemap_commands)

# --- hud.* / input commands (project.json data edited in memory) ---------------
add_executable(test_hud_input_commands test_hud_input_commands.cpp)
target_link_libraries(test_hud_input_commands PRIVATE arcane_tooling)
target_include_directories(test_hud_input_commands SYSTEM PRIVATE ${TP}/stb)
add_test(NAME test_hud_input_commands COMMAND test_hud_input_commands)

# --- clip.* commands (sheet grid slicing, clip files) ----------------------------
add_executable(test_clip_commands test_clip_commands.cpp)
target_link_libraries(test_clip_commands PRIVATE arcane_tooling)
target_include_directories(test_clip_commands SYSTEM PRIVATE ${TP}/stb)
add_test(NAME test_clip_commands COMMAND test_clip_commands)

# --- FE22 scene transitions, save API, transition.* / save.* commands ---------
add_executable(test_scene_flow test_scene_flow.cpp)
target_link_libraries(test_scene_flow PRIVATE arcane_tooling)
add_test(NAME test_scene_flow COMMAND test_scene_flow)

# --- FE23 debug overlay data, debug.* / bench.run commands, headless benchmark -
add_executable(test_debug test_debug.cpp)
target_link_libraries(test_debug PRIVATE arcane_tooling)
add_test(NAME test_debug COMMAND test_debug)

# CLI smoke: the benchmark prints arcane.bench JSON; bad arguments exit 2 without a device or GUI.
add_test(NAME bench_cli_smoke COMMAND arcane_bench --sprites 100 --emitters 2 --bodies 20 --frames 5)
set_tests_properties(bench_cli_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"format\": \"arcane.bench\"")
add_test(NAME bench_cli_bad_args COMMAND arcane_bench --frames 0)
set_tests_properties(bench_cli_bad_args PROPERTIES WILL_FAIL TRUE)
add_test(NAME bench_cli_bad_flag COMMAND arcane_bench --nope 1)
set_tests_properties(bench_cli_bad_flag PROPERTIES WILL_FAIL TRUE)

# --- AF12 deterministic replay: recording format, state hash, divergence, replay.* commands --------
add_executable(test_replay test_replay.cpp)
target_link_libraries(test_replay PRIVATE arcane_tooling)
add_test(NAME test_replay COMMAND test_replay)

if(ARCANE_BUILD_SAMPLES)
  add_test(NAME test_replay_player COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_replay_player.sh
    $<TARGET_FILE:arcane_player> ${CMAKE_SOURCE_DIR}/samples/platformer_slice $<TARGET_FILE:platformer_slice_game>
    ${CMAKE_SOURCE_DIR}/samples/platformer_slice/replays/walk_jump.replay.json)
  set_tests_properties(test_replay_player PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 120
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
  # AF10B: display-free fixed-frame run; deliberately no gl label and no display.
  add_test(NAME test_headless_player COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_headless_player.sh
    $<TARGET_FILE:arcane_player> ${CMAKE_SOURCE_DIR}/samples/sample_project $<TARGET_FILE:sample_game>
    ${CMAKE_SOURCE_DIR}/samples/platformer_slice $<TARGET_FILE:platformer_slice_game>
    ${CMAKE_SOURCE_DIR}/samples/platformer_slice/replays/walk_jump.replay.json)
  set_tests_properties(test_headless_player PROPERTIES TIMEOUT 120)
  # AF10B: author, run, test, export and run the export, all without a display.
  add_test(NAME test_cli_e2e COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_e2e.sh
    $<TARGET_FILE:arcane> $<TARGET_FILE:arcane_player> $<TARGET_FILE:sample_game>
    ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/samples/sample_project)
  set_tests_properties(test_cli_e2e PROPERTIES TIMEOUT 300)
  # AF10C: arcane render. Without a display it must fail cleanly with exit 8 (no gl label); the golden run needs GL.
  add_test(NAME test_render_cli_nodisplay COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_render_cli.sh nodisplay
    $<TARGET_FILE:arcane> ${CMAKE_SOURCE_DIR}/samples/platformer_slice $<TARGET_FILE:platformer_slice_game>
    ${CMAKE_CURRENT_SOURCE_DIR}/golden/render/platformer_slice_f30.png)
  set_tests_properties(test_render_cli_nodisplay PROPERTIES TIMEOUT 60 ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
  add_test(NAME test_render_cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_render_cli.sh gl
    $<TARGET_FILE:arcane> ${CMAKE_SOURCE_DIR}/samples/platformer_slice $<TARGET_FILE:platformer_slice_game>
    ${CMAKE_CURRENT_SOURCE_DIR}/golden/render/platformer_slice_f30.png)
  set_tests_properties(test_render_cli PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 120
    ENVIRONMENT "${ARCANE_GL_TEST_ENV}")
endif()

# AF12 rendering evidence: readback + PNG + golden comparison (tolerance documented in the test).
add_executable(test_render_golden test_render_golden.cpp)
target_link_libraries(test_render_golden PRIVATE arcane_runtime)
target_compile_definitions(test_render_golden PRIVATE ARCANE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME test_render_golden COMMAND test_render_golden)
set_tests_properties(test_render_golden PROPERTIES LABELS gl SKIP_RETURN_CODE 77 TIMEOUT 60
  ENVIRONMENT "${ARCANE_GL_TEST_ENV}")

add_executable(test_physics_events test_physics_events.cpp)
target_link_libraries(test_physics_events PRIVATE arcane_runtime)
add_test(NAME test_physics_events COMMAND test_physics_events)
