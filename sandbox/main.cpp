#include "core/swapchain.h"
#include "core/vulkan_context.h"
#include "core/window.h"

int main() {
  Paper::WindowConfig config{
      .width = 800, .height = 600, .title = "Paper Engine Window"};

  Paper::Window window(config);
  Paper::VulkanContext vulkan(window);
  Paper::Swapchain swapchain(vulkan, window);

  while (!window.shouldClose()) {
    window.update();
    if (window.framebufferWidth() != static_cast<int>(swapchain.extent().width) ||
        window.framebufferHeight() != static_cast<int>(swapchain.extent().height)) {
      swapchain.recreate(window);
    }
  }

  return 0;
}
