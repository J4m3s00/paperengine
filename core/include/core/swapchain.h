#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace Paper {
class VulkanContext;
class Window;

class Swapchain {
public:
  Swapchain(const VulkanContext &context, const Window &window);
  ~Swapchain();

  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;
  Swapchain(Swapchain &&) = delete;
  Swapchain &operator=(Swapchain &&) = delete;

  // Returns false (and keeps the old swapchain) while the framebuffer is 0x0,
  // e.g. when the window is minimised.
  bool recreate(const Window &window);

  [[nodiscard]] VkSwapchainKHR handle() const { return m_swapchain; }
  [[nodiscard]] VkFormat format() const { return m_format; }
  [[nodiscard]] VkExtent2D extent() const { return m_extent; }
  [[nodiscard]] const std::vector<VkImage> &images() const { return m_images; }
  [[nodiscard]] const std::vector<VkImageView> &imageViews() const {
    return m_imageViews;
  }

private:
  void build(int width, int height);
  void destroyImageViews();

  const VulkanContext &m_context;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  VkExtent2D m_extent{};
  std::vector<VkImage> m_images;
  std::vector<VkImageView> m_imageViews;
};
} // namespace Paper
