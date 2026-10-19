# Small ImGui tool to tune how each weapon sits in the character's hand.
add_executable(weapon_editor main.cpp)
target_link_libraries(weapon_editor PRIVATE core game mapview)
target_compile_definitions(weapon_editor PRIVATE PAPER_WEAPONS_DIR="${CMAKE_SOURCE_DIR}/weapons")
