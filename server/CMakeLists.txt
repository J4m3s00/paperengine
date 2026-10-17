# Headless dedicated server. Must never link core, GLFW or Vulkan.
add_executable(server main.cpp)
target_link_libraries(server PRIVATE game net)
target_compile_definitions(server PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps"
  PAPER_WEAPONS_DIR="${CMAKE_SOURCE_DIR}/weapons")
