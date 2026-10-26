add_library(sample_game MODULE sample_game.cpp)
target_link_libraries(sample_game PRIVATE arcane_runtime)
set_target_properties(sample_game PROPERTIES
  PREFIX ""
  CXX_VISIBILITY_PRESET hidden)

# samples/sample_project/project.json references this module by the fixed
# name "sample_game.so" (see docs/decisions.md); copy the freshly built
# module next to the project so the editor/player can load it without the
# source tree knowing the build directory's location. Not committed to git
# (build artifact, platform-specific name).
add_custom_command(TARGET sample_game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sample_game>
          ${CMAKE_SOURCE_DIR}/samples/sample_project/sample_game.so)
