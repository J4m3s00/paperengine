# Headless bot clients for soak tests and server benchmarks.
add_executable(bots main.cpp)
target_link_libraries(bots PRIVATE game net)
target_compile_definitions(bots PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps")
