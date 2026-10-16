# Dependency-free basics shared by every target, including the headless server.
add_library(base INTERFACE)
target_include_directories(base INTERFACE include)
target_link_libraries(base INTERFACE glm::glm spdlog::spdlog)
