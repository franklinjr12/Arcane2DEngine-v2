add_library(sample_game MODULE sample_game.cpp)
target_link_libraries(sample_game PRIVATE arcane_runtime)
set_target_properties(sample_game PROPERTIES
  PREFIX ""
  CXX_VISIBILITY_PRESET hidden)
