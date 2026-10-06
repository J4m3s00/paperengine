#pragma once

#include "core/error.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

struct VmaAllocator_T;
typedef VmaAllocator_T *VmaAllocator;

namespace Paper {
class Window;

struct VulkanConfig {
  std::string appName = "Paper Engine";
  bool enableValidation =
#ifdef NDEBUG
      false;
#else
      true;
#endif
};

// Owns instance, surface, device, queues and the VMA allocator.
// Must outlive every object created from it (e.g. Swapchain).
class VulkanContext {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<VulkanContext>, Error>
  create(const Window &window, const VulkanConfig &config = {});
  ~VulkanContext();

  VulkanContext(const VulkanContext &) = delete;
  VulkanContext &operator=(const VulkanContext &) = delete;
  VulkanContext(VulkanContext &&) = delete;
  VulkanContext &operator=(VulkanContext &&) = delete;

  [[nodiscard]] VkInstance instance() const { return m_instance; }
  [[nodiscard]] VkSurfaceKHR surface() const { return m_surface; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const {
    return m_physicalDevice;
  }
  [[nodiscard]] VkDevice device() const { return m_device; }
  [[nodiscard]] VmaAllocator allocator() const { return m_allocator; }

  [[nodiscard]] float maxSamplerAnisotropy() const {
    return m_maxSamplerAnisotropy;
  }

  [[nodiscard]] VkQueue graphicsQueue() const { return m_graphicsQueue; }
  [[nodiscard]] VkQueue presentQueue() const { return m_presentQueue; }
  [[nodiscard]] std::uint32_t graphicsQueueFamily() const {
    return m_graphicsFamily;
  }
  [[nodiscard]] std::uint32_t presentQueueFamily() const {
    return m_presentFamily;
  }

private:
  VulkanContext() = default;
  std::expected<void, Error> init(const Window &window,
                                  const VulkanConfig &config);

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;
  std::uint32_t m_graphicsFamily = 0;
  std::uint32_t m_presentFamily = 0;
  VmaAllocator m_allocator = nullptr;
  float m_maxSamplerAnisotropy = 1.0F;
};
} // namespace Paper
