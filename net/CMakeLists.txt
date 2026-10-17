# Protocol and transport (ADR 0009). Headless: no rendering or windowing.
add_library(net STATIC src/net.cpp src/loopback.cpp)
target_include_directories(net PUBLIC include)
target_link_libraries(net PUBLIC base)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)

add_executable(net_tests tests/main.cpp tests/loopback_test.cpp)
target_link_libraries(net_tests PRIVATE net doctest::doctest)
add_test(NAME net_tests COMMAND net_tests)
