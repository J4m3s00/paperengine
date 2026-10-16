# Game client: window, renderer, input, prediction, UI.
add_executable(client main.cpp)
target_link_libraries(client PRIVATE core game net)
