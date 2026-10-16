# Protocol and transport (ADR 0009). Headless: no rendering or windowing.
add_library(net STATIC src/net.cpp)
target_include_directories(net PUBLIC include)
target_link_libraries(net PUBLIC base)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)
