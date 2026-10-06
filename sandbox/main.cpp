#include "core/swapchain.h"
#include "core/vulkan_context.h"
#include "core/window.h"

#include <spdlog/spdlog.h>

int main() {
  Paper::WindowConfig config{
      .width = 800, .height = 600, .title = "Paper Engine Window"};

  auto window = Paper::Window::create(config);
  if (!window) {
    spdlog::critical("{}", window.error().message);
    return 1;
  }
  auto vulkan = Paper::VulkanContext::create(**window);
  if (!vulkan) {
    spdlog::critical("{}", vulkan.error().message);
    return 1;
  }
  auto swapchain = Paper::Swapchain::create(**vulkan, **window);
  if (!swapchain) {
    spdlog::critical("{}", swapchain.error().message);
    return 1;
  }

  while (!(*window)->shouldClose()) {
    (*window)->update();
    if ((*window)->framebufferWidth() !=
            static_cast<int>((*swapchain)->extent().width) ||
        (*window)->framebufferHeight() !=
            static_cast<int>((*swapchain)->extent().height)) {
      if (auto recreated = (*swapchain)->recreate(**window); !recreated) {
        spdlog::critical("{}", recreated.error().message);
        return 1;
      }
    }
  }

  return 0;
}
