# Skeletons, animation clips, pose blending and an animator. Headless: no
# rendering, so it is unit tested.
add_library(anim STATIC src/anim.cpp)
target_include_directories(anim PUBLIC include)
target_link_libraries(anim PUBLIC base)
target_compile_options(anim PRIVATE ${PAPER_WARNINGS})

add_executable(anim_tests tests/main.cpp tests/anim_test.cpp)
target_link_libraries(anim_tests PRIVATE anim doctest::doctest)
add_test(NAME anim_tests COMMAND anim_tests)
