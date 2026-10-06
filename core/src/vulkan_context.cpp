#include "core/vulkan_context.h"

#include "core/window.h"

#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>

#include "VkBootstrap.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

namespace Paper {
namespace {

template <typename T> T unwrap(vkb::Result<T> result, const char *what) {
  if (!result) {
    throw std::runtime_error(std::string(what) + ": " +
                             result.error().message());
  }
  return std::move(result.value());
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT /*type*/,
              const VkDebugUtilsMessengerCallbackDataEXT *data,
              void * /*userData*/) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    spdlog::error("[vulkan] {}", data->pMessage);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    spdlog::warn("[vulkan] {}", data->pMessage);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    spdlog::info("[vulkan] {}", data->pMessage);
  } else {
    spdlog::trace("[vulkan] {}", data->pMessage);
  }
  return VK_FALSE;
}
} // namespace

VulkanContext::VulkanContext(const Window &window, const VulkanConfig &config) {
  if (glfwVulkanSupported() == GLFW_FALSE) {
    throw std::runtime_error("GLFW: no Vulkan loader/ICD found");
  }

  vkb::InstanceBuilder instanceBuilder;
  instanceBuilder.set_app_name(config.appName.c_str())
      .set_engine_name("Paper Engine")
      .require_api_version(1, 3, 0);

  std::uint32_t glfwExtCount = 0;
  const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
  for (std::uint32_t i = 0; i < glfwExtCount; ++i) {
    instanceBuilder.enable_extension(glfwExts[i]);
  }

  if (config.enableValidation) {
    instanceBuilder.request_validation_layers().set_debug_callback(
        debugCallback);
  }

  vkb::Instance vkbInstance = unwrap(instanceBuilder.build(), "Instance");
  m_instance = vkbInstance.instance;
  m_debugMessenger = vkbInstance.debug_messenger;

  if (glfwCreateWindowSurface(m_instance, window.nativeHandle(), nullptr,
                              &m_surface) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create window surface");
  }

  VkPhysicalDeviceVulkan13Features features13{};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;

  vkb::PhysicalDevice vkbPhysical =
      unwrap(vkb::PhysicalDeviceSelector(vkbInstance, m_surface)
                 .set_minimum_version(1, 3)
                 .set_required_features_13(features13)
                 .select(),
             "Physical device");
  m_physicalDevice = vkbPhysical.physical_device;
  spdlog::info("Vulkan device: {}", vkbPhysical.name);

  vkb::Device vkbDevice =
      unwrap(vkb::DeviceBuilder(vkbPhysical).build(), "Logical device");
  m_device = vkbDevice.device;

  m_graphicsQueue =
      unwrap(vkbDevice.get_queue(vkb::QueueType::graphics), "Graphics queue");
  m_graphicsFamily = unwrap(vkbDevice.get_queue_index(vkb::QueueType::graphics),
                            "Graphics queue family");
  m_presentQueue =
      unwrap(vkbDevice.get_queue(vkb::QueueType::present), "Present queue");
  m_presentFamily = unwrap(vkbDevice.get_queue_index(vkb::QueueType::present),
                           "Present queue family");

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.instance = m_instance;
  allocatorInfo.physicalDevice = m_physicalDevice;
  allocatorInfo.device = m_device;
  if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create VMA allocator");
  }
}

VulkanContext::~VulkanContext() {
  if (m_allocator != nullptr) {
    vmaDestroyAllocator(m_allocator);
  }
  if (m_device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_device, nullptr);
  }
  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
  }
  if (m_debugMessenger != VK_NULL_HANDLE) {
    vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
  }
  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
  }
}
} // namespace Paper
