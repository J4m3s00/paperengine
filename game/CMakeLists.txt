# Shared deterministic simulation, compiled into client and server (ADR 0012).
# Must not depend on core, rendering, windowing or networking.
add_library(game STATIC src/game.cpp src/collision.cpp src/movement.cpp)
target_include_directories(game PUBLIC include)
target_link_libraries(game PUBLIC base)
target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)

add_executable(game_tests tests/main.cpp tests/tick_test.cpp tests/fixed_timestep_test.cpp tests/cvar_test.cpp tests/collision_test.cpp tests/movement_test.cpp)
target_link_libraries(game_tests PRIVATE game doctest::doctest)
add_test(NAME game_tests COMMAND game_tests)
