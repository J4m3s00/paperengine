cmake_minimum_required(VERSION 3.28)
# Static MSVC runtime so a Windows release runs without the VC++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
project(myengine LANGUAGES CXX VERSION 0.0.1)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
  set(PAPER_WARNINGS /W3)
else()
  set(PAPER_WARNINGS -Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.0)

FetchContent_Declare(entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG        v3.15.0)

FetchContent_Declare(spirv-reflect
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
  GIT_TAG        vulkan-sdk-1.4.363.0)
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# stb_image is a single header; fetch just that file at a pinned commit.
FetchContent_Declare(stb_image
  URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
  DOWNLOAD_NO_EXTRACT TRUE)

FetchContent_Declare(fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG        v0.9.1)

FetchContent_Declare(stb_image_write
  URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image_write.h
  DOWNLOAD_NO_EXTRACT TRUE)

FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.12)

FetchContent_Declare(tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
  GIT_TAG        v0.14.1)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0)
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_Declare(enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG        v1.3.18)
set(ENET_STATIC ON CACHE BOOL "" FORCE)

# miniaudio is a single header; it has a CMakeLists we do not want, so only
# fetch it (SOURCE_SUBDIR points at a folder that does not exist).
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.25
  SOURCE_SUBDIR  do_not_build)

# Dear ImGui has no CMake build; core/CMakeLists.txt builds it from source.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.93.0)

option(PAPER_ENABLE_PROFILER "Compile in Tracy profiler zones" OFF)
if(PAPER_ENABLE_PROFILER)
  # Only collect data while the Tracy viewer is connected.
  set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
  set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(tracy)
endif()

FetchContent_MakeAvailable(miniaudio enet nlohmann_json imgui doctest glfw glm spdlog entt spirv-reflect stb_image stb_image_write fastgltf)

enable_testing()

add_subdirectory(base)
add_subdirectory(game)
add_subdirectory(net)
add_subdirectory(audio)
add_subdirectory(core)
add_subdirectory(sandbox)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(bots)

# Release package: `cmake --install build/release --prefix dist/paperengine --component game`
# (or `cpack` for a zip). Data folders sit next to the executables, where
# Paper::dataDir and the renderer look for them.
install(TARGETS client server RUNTIME DESTINATION . COMPONENT game)
install(DIRECTORY maps weapons DESTINATION . COMPONENT game)
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders DESTINATION . COMPONENT game FILES_MATCHING PATTERN "*.spv")
install(FILES server.cfg packaging/README.txt DESTINATION . COMPONENT game)

set(CPACK_GENERATOR ZIP)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL game)
set(CPACK_PACKAGE_NAME "paperengine")
set(CPACK_PACKAGE_FILE_NAME "paperengine-${CMAKE_SYSTEM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
