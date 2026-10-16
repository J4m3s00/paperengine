# Shared deterministic simulation, compiled into client and server (ADR 0012).
# Must not depend on core, rendering, windowing or networking.
add_library(game STATIC src/game.cpp src/collision.cpp src/movement.cpp
  src/brush_geometry.cpp src/map.cpp)
target_include_directories(game PUBLIC include)
target_link_libraries(game PUBLIC base PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)

add_executable(game_tests tests/main.cpp tests/tick_test.cpp tests/fixed_timestep_test.cpp tests/cvar_test.cpp tests/collision_test.cpp tests/movement_test.cpp tests/map_test.cpp)
target_link_libraries(game_tests PRIVATE game doctest::doctest)
add_test(NAME game_tests COMMAND game_tests)
target_compile_definitions(game_tests PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps")
