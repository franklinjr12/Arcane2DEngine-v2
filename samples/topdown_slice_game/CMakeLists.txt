add_library(topdown_slice_game MODULE topdown_slice_game.cpp)
target_link_libraries(topdown_slice_game PRIVATE arcane_runtime)
set_target_properties(topdown_slice_game PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
add_custom_command(TARGET topdown_slice_game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:topdown_slice_game>
          ${CMAKE_SOURCE_DIR}/samples/topdown_slice/topdown_slice_game.so)
