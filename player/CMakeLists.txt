add_executable(arcane_player player.cpp)
target_link_libraries(arcane_player PRIVATE arcane_runtime arcane_editor_core)
add_dependencies(arcane_player sample_game)
target_compile_definitions(arcane_player PRIVATE
  ARCANE_DEFAULT_MODULE_PATH="$<TARGET_FILE:sample_game>")
