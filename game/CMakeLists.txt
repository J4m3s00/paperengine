# Shared deterministic simulation, compiled into client and server (ADR 0012).
# Must not depend on core, rendering, windowing or networking.
add_library(game STATIC src/game.cpp)
target_include_directories(game PUBLIC include)
target_link_libraries(game PUBLIC base)
target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
