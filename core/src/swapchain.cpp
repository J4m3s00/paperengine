#include "core/swapchain.h"

#include "core/vulkan_context.h"
#include "core/window.h"

#include "vkb_error.h"

namespace Paper {

std::expected<std::unique_ptr<Swapchain>, Error>
Swapchain::create(const VulkanContext &context, const Window &window) {
  std::unique_ptr<Swapchain> swapchain(new Swapchain(context));
  if (auto built = swapchain->build(window.framebufferWidth(),
                                    window.framebufferHeight());
      !built) {
    return std::unexpected(built.error());
  }
  return swapchain;
}

Swapchain::~Swapchain() {
  destroyImageViews();
  if (m_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_context.device(), m_swapchain, nullptr);
  }
}

std::expected<bool, Error> Swapchain::recreate(const Window &window) {
  const int width = window.framebufferWidth();
  const int height = window.framebufferHeight();
  if (width == 0 || height == 0) {
    return false;
  }
  vkDeviceWaitIdle(m_context.device());
  if (auto built = build(width, height); !built) {
    return std::unexpected(built.error());
  }
  return true;
}

std::expected<void, Error> Swapchain::build(int width, int height) {
  vkb::SwapchainBuilder builder{m_context.physicalDevice(), m_context.device(),
                                m_context.surface(),
                                m_context.graphicsQueueFamily(),
                                m_context.presentQueueFamily()};
  auto built =
      builder
          .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB,
                               VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
          .set_desired_present_mode(m_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
          .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
          .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
          .set_desired_extent(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height))
          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
          .set_old_swapchain(m_swapchain)
          .build();
  auto result = unwrap(std::move(built), ErrorCode::Swapchain, "Swapchain");
  if (!result) {
    return std::unexpected(result.error());
  }
  vkb::Swapchain vkbSwapchain = std::move(*result);

  destroyImageViews();
  if (m_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(m_context.device(), m_swapchain, nullptr);
  }

  m_swapchain = vkbSwapchain.swapchain;
  m_format = vkbSwapchain.image_format;
  m_extent = vkbSwapchain.extent;

  auto images = vkbSwapchain.get_images();
  auto views = vkbSwapchain.get_image_views();
  if (!images || !views) {
    return std::unexpected(Error{ErrorCode::Swapchain,
                                 "Swapchain: failed to get images/views"});
  }
  m_images = std::move(images.value());
  m_imageViews = std::move(views.value());
  return {};
}

void Swapchain::destroyImageViews() {
  for (VkImageView view : m_imageViews) {
    vkDestroyImageView(m_context.device(), view, nullptr);
  }
  m_imageViews.clear();
}
} // namespace Paper
