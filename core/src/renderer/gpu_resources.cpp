#include "gpu_resources.h"

#include "core/vulkan_context.h"

#include "vk_check.h"

#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>

#include <cstring>
#include <utility>

namespace Paper {
namespace {
constexpr ErrorCode kCode = ErrorCode::Vulkan;
}

GpuBuffer::~GpuBuffer() { destroy(); }

GpuBuffer::GpuBuffer(GpuBuffer &&other) noexcept
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, nullptr)),
      m_size(std::exchange(other.m_size, 0)),
      m_mapped(std::exchange(other.m_mapped, nullptr)) {}

GpuBuffer &GpuBuffer::operator=(GpuBuffer &&other) noexcept {
  if (this != &other) {
    destroy();
    m_allocator = std::exchange(other.m_allocator, nullptr);
    m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
    m_allocation = std::exchange(other.m_allocation, nullptr);
    m_size = std::exchange(other.m_size, 0);
    m_mapped = std::exchange(other.m_mapped, nullptr);
  }
  return *this;
}

void GpuBuffer::destroy() {
  if (m_buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
  }
  m_buffer = VK_NULL_HANDLE;
  m_allocation = nullptr;
  m_mapped = nullptr;
}

std::expected<GpuBuffer, Error>
GpuBuffer::create(const VulkanContext &context, VkDeviceSize size,
                  VkBufferUsageFlags usage, bool hostVisible) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (hostVisible) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  GpuBuffer buffer;
  VmaAllocationInfo info{};
  if (auto r = vkCheck(vmaCreateBuffer(context.allocator(), &bufferInfo,
                                       &allocInfo, &buffer.m_buffer,
                                       &buffer.m_allocation, &info),
                       kCode, "vmaCreateBuffer");
      !r) {
    return std::unexpected(r.error());
  }
  buffer.m_allocator = context.allocator();
  buffer.m_size = size;
  buffer.m_mapped = hostVisible ? info.pMappedData : nullptr;
  return buffer;
}

void GpuBuffer::flush() const {
  vmaFlushAllocation(m_allocator, m_allocation, 0, VK_WHOLE_SIZE);
}

GpuImage::~GpuImage() { destroy(); }

GpuImage::GpuImage(GpuImage &&other) noexcept
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_image(std::exchange(other.m_image, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, nullptr)),
      m_view(std::exchange(other.m_view, VK_NULL_HANDLE)),
      m_format(other.m_format), m_extent(other.m_extent) {}

GpuImage &GpuImage::operator=(GpuImage &&other) noexcept {
  if (this != &other) {
    destroy();
    m_allocator = std::exchange(other.m_allocator, nullptr);
    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
    m_allocation = std::exchange(other.m_allocation, nullptr);
    m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
    m_format = other.m_format;
    m_extent = other.m_extent;
  }
  return *this;
}

void GpuImage::destroy() {
  if (m_view != VK_NULL_HANDLE) {
    vkDestroyImageView(m_device, m_view, nullptr);
  }
  if (m_image != VK_NULL_HANDLE) {
    vmaDestroyImage(m_allocator, m_image, m_allocation);
  }
  m_view = VK_NULL_HANDLE;
  m_image = VK_NULL_HANDLE;
  m_allocation = nullptr;
}

std::expected<GpuImage, Error>
GpuImage::create(const VulkanContext &context, VkExtent2D extent, VkFormat format,
                 VkImageUsageFlags usage, VkImageAspectFlags aspect) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent = {extent.width, extent.height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = usage;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

  GpuImage image;
  if (auto r = vkCheck(vmaCreateImage(context.allocator(), &imageInfo,
                                      &allocInfo, &image.m_image,
                                      &image.m_allocation, nullptr),
                       kCode, "vmaCreateImage");
      !r) {
    return std::unexpected(r.error());
  }
  image.m_allocator = context.allocator();
  image.m_device = context.device();
  image.m_format = format;
  image.m_extent = extent;

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image.m_image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
  if (auto r = vkCheck(
          vkCreateImageView(context.device(), &viewInfo, nullptr, &image.m_view),
          kCode, "vkCreateImageView");
      !r) {
    return std::unexpected(r.error());
  }
  return image;
}

std::expected<GpuBuffer, Error> uploadBuffer(const VulkanContext &context,
                                             const void *data, VkDeviceSize size,
                                             VkBufferUsageFlags usage) {
  auto staging = GpuBuffer::create(context, size,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  if (!staging) {
    return std::unexpected(staging.error());
  }
  std::memcpy(staging->mapped(), data, static_cast<std::size_t>(size));
  staging->flush();

  auto target = GpuBuffer::create(
      context, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  if (!target) {
    return std::unexpected(target.error());
  }

  const VkDevice device = context.device();

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
  VkCommandPool pool = VK_NULL_HANDLE;
  if (auto r = vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool),
                       kCode, "vkCreateCommandPool");
      !r) {
    return std::unexpected(r.error());
  }

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;

  auto submit = [&]() -> std::expected<void, Error> {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (auto r = vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &cmd),
                         kCode, "vkAllocateCommandBuffers");
        !r) {
      return r;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    const VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging->handle(), target->handle(), 1, &region);
    vkEndCommandBuffer(cmd);

    if (auto r = vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &fence),
                         kCode, "vkCreateFence");
        !r) {
      return r;
    }
    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    if (auto r = vkCheck(
            vkQueueSubmit2(context.graphicsQueue(), 1, &submitInfo, fence),
            kCode, "vkQueueSubmit2");
        !r) {
      return r;
    }
    return vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
                   kCode, "vkWaitForFences");
  };

  auto result = submit();
  vkDestroyFence(device, fence, nullptr);
  vkDestroyCommandPool(device, pool, nullptr);
  if (!result) {
    return std::unexpected(result.error());
  }
  return std::move(*target);
}
} // namespace Paper
