add_executable(arcane_editor editor_main.cpp)
target_link_libraries(arcane_editor PRIVATE arcane_tooling arcane_editor_core arcane_runtime imgui glad)
# Not a link dependency, just build-order: sample_game's POST_BUILD step
# copies its module next to samples/sample_project/project.json, which the
# gl-labelled test_editor_frames CTest entry loads at runtime.
add_dependencies(arcane_editor sample_game)
