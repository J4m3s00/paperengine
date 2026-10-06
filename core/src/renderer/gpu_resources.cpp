#include "gpu_resources.h"

#include "core/vulkan_context.h"

#include "vk_check.h"

#define VMA_VULKAN_VERSION 1003000
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <functional>
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
                  VkBufferUsageFlags usage, bool hostVisible, bool hostRead) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (hostVisible) {
    allocInfo.flags = (hostRead ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) |
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

void GpuBuffer::invalidate() const {
  vmaInvalidateAllocation(m_allocator, m_allocation, 0, VK_WHOLE_SIZE);
}

GpuImage::~GpuImage() { destroy(); }

GpuImage::GpuImage(GpuImage &&other) noexcept
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_image(std::exchange(other.m_image, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, nullptr)),
      m_view(std::exchange(other.m_view, VK_NULL_HANDLE)),
      m_format(other.m_format), m_extent(other.m_extent),
      m_mipLevels(other.m_mipLevels) {}

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
    m_mipLevels = other.m_mipLevels;
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
                 VkImageUsageFlags usage, VkImageAspectFlags aspect,
                 std::uint32_t mipLevels) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent = {extent.width, extent.height, 1};
  imageInfo.mipLevels = mipLevels;
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
  image.m_mipLevels = mipLevels;

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image.m_image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange = {aspect, 0, mipLevels, 0, 1};
  if (auto r = vkCheck(
          vkCreateImageView(context.device(), &viewInfo, nullptr, &image.m_view),
          kCode, "vkCreateImageView");
      !r) {
    return std::unexpected(r.error());
  }
  return image;
}

namespace {
// Records `record` into a one-shot command buffer on the graphics queue and
// waits for it to finish.
std::expected<void, Error>
submitOneShot(const VulkanContext &context,
              const std::function<void(VkCommandBuffer)> &record) {
  const VkDevice device = context.device();

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
  VkCommandPool pool = VK_NULL_HANDLE;
  if (auto r = vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool),
                       kCode, "vkCreateCommandPool");
      !r) {
    return r;
  }

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
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
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
  return result;
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, std::uint32_t baseMip,
                  std::uint32_t mipCount, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1};

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}
} // namespace

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

  auto submitted = submitOneShot(context, [&](VkCommandBuffer cmd) {
    const VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging->handle(), target->handle(), 1, &region);
  });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  return std::move(*target);
}

std::expected<GpuImage, Error>
uploadTexture(const VulkanContext &context, const std::uint8_t *pixels,
              std::uint32_t width, std::uint32_t height, VkFormat format) {
  if (width == 0 || height == 0) {
    return std::unexpected(
        Error{ErrorCode::InvalidArgument, "Texture has zero size"});
  }

  VkFormatProperties formatProperties{};
  vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), format,
                                      &formatProperties);
  constexpr VkFormatFeatureFlags kRequired =
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if ((formatProperties.optimalTilingFeatures & kRequired) != kRequired) {
    return std::unexpected(
        Error{kCode, "Texture format does not support linear blits"});
  }

  std::uint32_t mipLevels = 1;
  for (std::uint32_t size = std::max(width, height); size > 1; size >>= 1) {
    ++mipLevels;
  }

  const VkDeviceSize byteSize = static_cast<VkDeviceSize>(width) * height * 4;
  auto staging = GpuBuffer::create(context, byteSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  if (!staging) {
    return std::unexpected(staging.error());
  }
  std::memcpy(staging->mapped(), pixels, static_cast<std::size_t>(byteSize));
  staging->flush();

  auto image = GpuImage::create(
      context, {width, height}, format,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
  if (!image) {
    return std::unexpected(image.error());
  }

  constexpr VkPipelineStageFlags2 kTransfer = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  constexpr VkPipelineStageFlags2 kShader =
      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

  auto submitted = submitOneShot(context, [&](VkCommandBuffer cmd) {
    const VkImage vkImage = image->image();

    imageBarrier(cmd, vkImage, 0, mipLevels, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_NONE, 0, kTransfer,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging->handle(), vkImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    std::int32_t mipWidth = static_cast<std::int32_t>(width);
    std::int32_t mipHeight = static_cast<std::int32_t>(height);
    for (std::uint32_t level = 1; level < mipLevels; ++level) {
      // The previous level was just written; make it a blit source.
      imageBarrier(cmd, vkImage, level - 1, 1,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kTransfer,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, kTransfer,
                   VK_ACCESS_2_TRANSFER_READ_BIT);

      const std::int32_t nextWidth = std::max(mipWidth / 2, 1);
      const std::int32_t nextHeight = std::max(mipHeight / 2, 1);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
      blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
      blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
      vkCmdBlitImage(cmd, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkImage,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);

      // The previous level is finished.
      imageBarrier(cmd, vkImage, level - 1, 1,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer,
                   VK_ACCESS_2_TRANSFER_READ_BIT, kShader,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      mipWidth = nextWidth;
      mipHeight = nextHeight;
    }

    // The last level was only ever written.
    imageBarrier(cmd, vkImage, mipLevels - 1, 1,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT, kShader,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  return std::move(*image);
}
} // namespace Paper
