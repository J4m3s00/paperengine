# Headless dedicated server. Must never link core, GLFW or Vulkan.
add_executable(server main.cpp)
target_link_libraries(server PRIVATE game net)
