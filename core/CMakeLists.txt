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
  src/vma_impl.cpp)
target_include_directories(core PUBLIC include)
target_link_libraries(core
  PUBLIC  glfw glm::glm spdlog::spdlog Vulkan::Vulkan
  PRIVATE vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(core PRIVATE GLFW_INCLUDE_NONE)

# VMA is a third-party single header; keep its warnings out of our build.
get_target_property(VMA_INCLUDE_DIRS GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(core SYSTEM PRIVATE ${VMA_INCLUDE_DIRS})
