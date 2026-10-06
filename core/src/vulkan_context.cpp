#include "core/vulkan_context.h"

#include "core/window.h"

#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>

#include "vkb_error.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <string>

namespace Paper {
namespace {

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

std::expected<std::unique_ptr<VulkanContext>, Error>
VulkanContext::create(const Window &window, const VulkanConfig &config) {
  std::unique_ptr<VulkanContext> context(new VulkanContext());
  if (auto initialised = context->init(window, config); !initialised) {
    return std::unexpected(initialised.error());
  }
  return context;
}

std::expected<void, Error> VulkanContext::init(const Window &window,
                                               const VulkanConfig &config) {
  constexpr ErrorCode code = ErrorCode::Vulkan;

  if (glfwVulkanSupported() == GLFW_FALSE) {
    return std::unexpected(
        Error{code, "GLFW: no Vulkan loader/ICD found"});
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

  auto vkbInstance = unwrap(instanceBuilder.build(), code, "Instance");
  if (!vkbInstance) {
    return std::unexpected(vkbInstance.error());
  }
  m_instance = vkbInstance->instance;
  m_debugMessenger = vkbInstance->debug_messenger;

  if (glfwCreateWindowSurface(m_instance, window.nativeHandle(), nullptr,
                              &m_surface) != VK_SUCCESS) {
    return std::unexpected(Error{code, "Failed to create window surface"});
  }

  VkPhysicalDeviceVulkan13Features features13{};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;

  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = VK_TRUE;

  auto vkbPhysical =
      unwrap(vkb::PhysicalDeviceSelector(*vkbInstance, m_surface)
                 .set_minimum_version(1, 3)
                 .set_required_features(features)
                 .set_required_features_13(features13)
                 .select(),
             code, "Physical device");
  if (!vkbPhysical) {
    return std::unexpected(vkbPhysical.error());
  }
  m_physicalDevice = vkbPhysical->physical_device;
  spdlog::info("Vulkan device: {}", vkbPhysical->name);
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
  m_maxSamplerAnisotropy = properties.limits.maxSamplerAnisotropy;

  auto vkbDevice =
      unwrap(vkb::DeviceBuilder(*vkbPhysical).build(), code, "Logical device");
  if (!vkbDevice) {
    return std::unexpected(vkbDevice.error());
  }
  m_device = vkbDevice->device;

  auto graphicsQueue =
      unwrap(vkbDevice->get_queue(vkb::QueueType::graphics), code,
             "Graphics queue");
  if (!graphicsQueue) {
    return std::unexpected(graphicsQueue.error());
  }
  m_graphicsQueue = *graphicsQueue;

  auto graphicsFamily =
      unwrap(vkbDevice->get_queue_index(vkb::QueueType::graphics), code,
             "Graphics queue family");
  if (!graphicsFamily) {
    return std::unexpected(graphicsFamily.error());
  }
  m_graphicsFamily = *graphicsFamily;

  auto presentQueue = unwrap(vkbDevice->get_queue(vkb::QueueType::present),
                             code, "Present queue");
  if (!presentQueue) {
    return std::unexpected(presentQueue.error());
  }
  m_presentQueue = *presentQueue;

  auto presentFamily =
      unwrap(vkbDevice->get_queue_index(vkb::QueueType::present), code,
             "Present queue family");
  if (!presentFamily) {
    return std::unexpected(presentFamily.error());
  }
  m_presentFamily = *presentFamily;

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.instance = m_instance;
  allocatorInfo.physicalDevice = m_physicalDevice;
  allocatorInfo.device = m_device;
  if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
    return std::unexpected(Error{code, "Failed to create VMA allocator"});
  }
  return {};
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
