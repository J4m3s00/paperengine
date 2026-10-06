#pragma once

#include "core/error.h"
#include "core/vulkan_context.h"

#include <glm/glm.hpp>

#include <expected>
#include <memory>

namespace Paper {
class Window;

struct RendererConfig {
  VulkanConfig vulkan;
  // Linear color; the swapchain is sRGB.
  glm::vec4 clearColor{0.02F, 0.02F, 0.05F, 1.0F};
};

// Owns the VulkanContext, the Swapchain and all per-frame resources.
// `window` must outlive the Renderer.
class Renderer {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<Renderer>, Error>
  create(const Window &window, const RendererConfig &config = {});
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) = delete;
  Renderer &operator=(Renderer &&) = delete;

  // Renders and presents one frame. Handles resize and minimise internally;
  // while the window is minimised this is a no-op.
  [[nodiscard]] std::expected<void, Error> render();

private:
  struct Impl;
  explicit Renderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> m_pImpl;
};
} // namespace Paper
