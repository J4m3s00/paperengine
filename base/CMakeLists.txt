# Dependency-light basics shared by every target, including the headless server.
add_library(base STATIC src/cvar.cpp)
target_include_directories(base PUBLIC include)
target_link_libraries(base PUBLIC glm::glm spdlog::spdlog)
target_compile_options(base PRIVATE -Wall -Wextra -Wpedantic)
