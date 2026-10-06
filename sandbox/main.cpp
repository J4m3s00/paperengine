#include "core/renderer/renderer.h"
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
  auto renderer = Paper::Renderer::create(**window);
  if (!renderer) {
    spdlog::critical("{}", renderer.error().message);
    return 1;
  }

  while (!(*window)->shouldClose()) {
    (*window)->update();
    if (auto rendered = (*renderer)->render(); !rendered) {
      spdlog::critical("{}", rendered.error().message);
      return 1;
    }
  }

  return 0;
}
