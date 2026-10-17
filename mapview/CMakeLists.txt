# Rendering of maps shared by the game client and the editor: brush faces to
# meshes, the placeholder material palette.
add_library(mapview STATIC src/world_mesh.cpp)
target_include_directories(mapview PUBLIC include)
target_link_libraries(mapview PUBLIC core game)
target_compile_options(mapview PRIVATE ${PAPER_WARNINGS})
