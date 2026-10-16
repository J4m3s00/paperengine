#pragma once

#include "base/error.h"

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>
#include <vector>

namespace Paper {
class VulkanContext;
class Window;

class Swapchain {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<Swapchain>, Error>
  create(const VulkanContext &context, const Window &window);
  ~Swapchain();

  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain(Swapchain &&) = delete;
  Swapchain &operator=(Swapchain &&) = delete;

  // Returns false (and keeps the old swapchain) while the framebuffer is 0x0,
  // e.g. when the window is minimised.
  [[nodiscard]] std::expected<bool, Error> recreate(const Window &window);

  [[nodiscard]] VkSwapchainKHR handle() const { return m_swapchain; }
  [[nodiscard]] VkFormat format() const { return m_format; }
  [[nodiscard]] VkExtent2D extent() const { return m_extent; }
  [[nodiscard]] const std::vector<VkImage> &images() const { return m_images; }
  [[nodiscard]] const std::vector<VkImageView> &imageViews() const {
    return m_imageViews;
  }

private:
  explicit Swapchain(const VulkanContext &context) : m_context(context) {}
  std::expected<void, Error> build(int width, int height);
  void destroyImageViews();

  const VulkanContext &m_context;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  VkExtent2D m_extent{};
  std::vector<VkImage> m_images;
  std::vector<VkImageView> m_imageViews;
};
} // namespace Paper
