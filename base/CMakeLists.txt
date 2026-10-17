# Dependency-light basics shared by every target, including the headless server.
add_library(base STATIC src/cvar.cpp)
target_include_directories(base PUBLIC include)
target_link_libraries(base PUBLIC glm::glm spdlog::spdlog)
target_compile_options(base PRIVATE ${PAPER_WARNINGS})

if(PAPER_ENABLE_PROFILER)
  # Third-party sources: keep their warnings out of our build.
  target_compile_options(TracyClient PRIVATE -w)
  target_link_libraries(base PUBLIC Tracy::TracyClient)
  target_compile_definitions(base PUBLIC PAPER_PROFILER)
endif()
