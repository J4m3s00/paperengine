# Protocol and transport (ADR 0009). Headless: no rendering or windowing.
add_library(net STATIC src/net.cpp src/loopback.cpp src/enet_transport.cpp src/protocol.cpp src/server.cpp src/client_session.cpp src/conditioned_transport.cpp)
target_include_directories(net PUBLIC include)
target_link_libraries(net PUBLIC base game PRIVATE enet)
target_include_directories(net SYSTEM PRIVATE ${enet_SOURCE_DIR}/include)
target_compile_options(net PRIVATE ${PAPER_WARNINGS})

add_executable(net_tests tests/main.cpp tests/loopback_test.cpp tests/enet_test.cpp tests/protocol_test.cpp tests/session_test.cpp)
target_link_libraries(net_tests PRIVATE net doctest::doctest)
target_compile_definitions(net_tests PRIVATE PAPER_WEAPONS_DIR="${CMAKE_SOURCE_DIR}/weapons")
add_test(NAME net_tests COMMAND net_tests)
