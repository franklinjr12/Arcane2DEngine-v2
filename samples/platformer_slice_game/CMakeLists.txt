add_library(platformer_slice_game MODULE platformer_slice_game.cpp)
target_link_libraries(platformer_slice_game PRIVATE arcane_runtime)
set_target_properties(platformer_slice_game PROPERTIES
  PREFIX ""
  CXX_VISIBILITY_PRESET hidden)

# samples/platformer_slice/project.json names this module "platformer_slice_game.so"
# (same convention as sample_game, see docs/decisions.md); copy the built module next to the project.
add_custom_command(TARGET platformer_slice_game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:platformer_slice_game>
          ${CMAKE_SOURCE_DIR}/samples/platformer_slice/platformer_slice_game.so)
