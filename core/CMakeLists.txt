find_package(Vulkan REQUIRED COMPONENTS glslc)

FetchContent_Declare(vk-bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG        v1.4.363)

FetchContent_Declare(VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.3.0)

FetchContent_MakeAvailable(vk-bootstrap VulkanMemoryAllocator)

add_library(core STATIC
  src/window.cpp
  src/engine.cpp
  src/vulkan_context.cpp
  src/swapchain.cpp
  src/renderer/renderer.cpp
  src/renderer/shader.cpp
  src/vma_impl.cpp)
target_include_directories(core PUBLIC include PRIVATE src)
target_link_libraries(core
  PUBLIC  glfw glm::glm spdlog::spdlog Vulkan::Vulkan
  PRIVATE vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(core PRIVATE GLFW_INCLUDE_NONE)

# VMA is a third-party single header; keep its warnings out of our build.
get_target_property(VMA_INCLUDE_DIRS GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(core SYSTEM PRIVATE ${VMA_INCLUDE_DIRS})

# Compile GLSL shaders to SPIR-V in <build>/shaders at build time.
set(PAPER_SHADER_SOURCES
  shaders/triangle.vert
  shaders/triangle.frag)
set(PAPER_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
set(PAPER_SHADER_OUTPUTS)
foreach(shader IN LISTS PAPER_SHADER_SOURCES)
  get_filename_component(shader_name ${shader} NAME)
  set(shader_out ${PAPER_SHADER_OUTPUT_DIR}/${shader_name}.spv)
  add_custom_command(
    OUTPUT ${shader_out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PAPER_SHADER_OUTPUT_DIR}
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${shader_out}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
    COMMENT "glslc ${shader}"
    VERBATIM)
  list(APPEND PAPER_SHADER_OUTPUTS ${shader_out})
endforeach()
add_custom_target(core_shaders DEPENDS ${PAPER_SHADER_OUTPUTS})
add_dependencies(core core_shaders)

# Dev default for RendererConfig::assetRoot when no <exe dir>/shaders exists.
target_compile_definitions(core PRIVATE
  PAPER_DEFAULT_ASSET_ROOT="${CMAKE_BINARY_DIR}")
