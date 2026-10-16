#pragma once

#include "base/error.h"

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>

namespace Paper {
class VulkanContext;
class Window;

// Owns the Dear ImGui context plus its GLFW and Vulkan backends. Draws into the
// renderer's dynamic-rendering pass, so the pass formats must match `create`.
class ImGuiLayer {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<ImGuiLayer>, Error>
  create(const Window &window, const VulkanContext &context,
         VkFormat colorFormat, VkFormat depthFormat, std::uint32_t imageCount);
  ~ImGuiLayer();

  ImGuiLayer(const ImGuiLayer &) = delete;
  ImGuiLayer &operator=(const ImGuiLayer &) = delete;
  ImGuiLayer(ImGuiLayer &&) = delete;
  ImGuiLayer &operator=(ImGuiLayer &&) = delete;

  // Starts an ImGui frame; widget calls are valid until `endFrame`.
  void beginFrame();
  // Finalizes the frame's draw data. Must follow every `beginFrame`.
  void endFrame();
  // Records the draw data of the last `endFrame` into `cmd` (inside the pass).
  void record(VkCommandBuffer cmd) const;

  [[nodiscard]] bool wantsMouse() const;
  [[nodiscard]] bool wantsKeyboard() const;

private:
  ImGuiLayer() = default;
};
} // namespace Paper
