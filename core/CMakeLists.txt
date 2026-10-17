find_package(Vulkan REQUIRED COMPONENTS glslc)

FetchContent_Declare(vk-bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG        v1.4.363)

FetchContent_Declare(VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.3.0)

FetchContent_MakeAvailable(vk-bootstrap VulkanMemoryAllocator)

# Dear ImGui (core + GLFW and Vulkan backends), built from the fetched sources.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
target_compile_options(imgui PRIVATE -w)

add_library(core STATIC
  src/window.cpp
  src/engine.cpp
  src/vulkan_context.cpp
  src/swapchain.cpp
  src/renderer/renderer.cpp
  src/renderer/imgui_layer.cpp
  src/debug/debug_overlay.cpp
  src/renderer/shader.cpp
  src/renderer/camera.cpp
  src/renderer/mesh.cpp
  src/renderer/gpu_resources.cpp
  src/renderer/pipeline.cpp
  src/renderer/shader_reflection.cpp
  src/assets/image.cpp
  src/assets/image_write.cpp
  src/assets/gltf_loader.cpp
  src/scene/scene.cpp
  src/scene/transform_system.cpp
  src/vma_impl.cpp)
target_include_directories(core PUBLIC include PRIVATE src)
target_link_libraries(core
  PUBLIC  base imgui glfw glm::glm spdlog::spdlog EnTT::EnTT Vulkan::Vulkan
  PRIVATE vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator
          spirv-reflect-static
          fastgltf::fastgltf)
target_compile_options(core PRIVATE ${PAPER_WARNINGS})
target_compile_definitions(core PRIVATE GLFW_INCLUDE_NONE)

# VMA is a third-party single header; keep its warnings out of our build.
get_target_property(VMA_INCLUDE_DIRS GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(core SYSTEM PRIVATE ${VMA_INCLUDE_DIRS})

# stb_image.h is downloaded as a single file into stb_image_SOURCE_DIR.
target_include_directories(core SYSTEM PRIVATE ${stb_image_SOURCE_DIR}
                                               ${stb_image_write_SOURCE_DIR})

# Same for SPIRV-Reflect.
get_target_property(SPIRV_REFLECT_INCLUDE_DIRS spirv-reflect-static INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(core SYSTEM PRIVATE ${SPIRV_REFLECT_INCLUDE_DIRS})

# Compile GLSL shaders to SPIR-V in <build>/shaders at build time.
set(PAPER_SHADER_SOURCES
  shaders/pbr.vert
  shaders/pbr_skinned.vert
  shaders/pbr.frag)
set(PAPER_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
set(PAPER_SHADER_OUTPUTS)
foreach(shader IN LISTS PAPER_SHADER_SOURCES)
  get_filename_component(shader_name ${shader} NAME)
  set(shader_out ${PAPER_SHADER_OUTPUT_DIR}/${shader_name}.spv)
  add_custom_command(
    OUTPUT ${shader_out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PAPER_SHADER_OUTPUT_DIR}
    COMMAND Vulkan::glslc ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${shader_out}
            -MD -MF ${shader_out}.d -MT ${shader_out}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
    DEPFILE ${shader_out}.d
    COMMENT "glslc ${shader}"
    VERBATIM)
  list(APPEND PAPER_SHADER_OUTPUTS ${shader_out})
endforeach()
add_custom_target(core_shaders DEPENDS ${PAPER_SHADER_OUTPUTS})
add_dependencies(core core_shaders)

# Dev default for RendererConfig::assetRoot when no <exe dir>/shaders exists.
target_compile_definitions(core PRIVATE
  PAPER_DEFAULT_ASSET_ROOT="${CMAKE_BINARY_DIR}")
