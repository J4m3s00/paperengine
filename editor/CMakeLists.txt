# In-engine map editor (ImGui): edits the same JSON maps the game loads.
add_executable(editor main.cpp)
target_link_libraries(editor PRIVATE core game mapview)
target_compile_definitions(editor PRIVATE PAPER_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps")

# Command line map checker (no window): `mapc map.json`.
add_executable(mapc mapc.cpp)
target_link_libraries(mapc PRIVATE game)
