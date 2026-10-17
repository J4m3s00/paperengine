# Game client: window, renderer, input, prediction, UI.
add_executable(client main.cpp)
target_link_libraries(client PRIVATE core game net audio mapview anim)
target_compile_definitions(client PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps"
  PAPER_WEAPONS_DIR="${CMAKE_SOURCE_DIR}/weapons")
