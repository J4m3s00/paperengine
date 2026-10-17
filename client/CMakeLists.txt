# Game client: window, renderer, input, prediction, UI.
add_executable(client main.cpp world_mesh.cpp)
target_link_libraries(client PRIVATE core game net)
target_compile_definitions(client PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps"
  PAPER_WEAPONS_DIR="${CMAKE_SOURCE_DIR}/weapons")
