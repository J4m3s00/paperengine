#include "core/renderer/renderer.h"

#include "core/swapchain.h"
#include "core/vulkan_context.h"
#include "core/window.h"

#include "vk_check.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace Paper {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr ErrorCode kCode = ErrorCode::Vulkan;

struct FrameData {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkFence inFlight = VK_NULL_HANDLE;
};

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from,
                     VkImageLayout to, VkPipelineStageFlags2 srcStage,
                     VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess) {
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
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}
} // namespace

struct Renderer::Impl {
  const Window &window;
  RendererConfig config;
  std::unique_ptr<VulkanContext> context;
  std::unique_ptr<Swapchain> swapchain;
  std::array<FrameData, kFramesInFlight> frames{};
  // One per swapchain image: presentation may still hold the semaphore after
  // the frame that signalled it has finished.
  std::vector<VkSemaphore> renderFinished;
  std::uint32_t frameIndex = 0;

  Impl(const Window &w, RendererConfig c) : window(w), config(std::move(c)) {}
  ~Impl();

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;

  std::expected<void, Error> init();
  std::expected<void, Error> createFrameResources();
  std::expected<void, Error> createRenderFinishedSemaphores();
  void destroyRenderFinishedSemaphores();
  // Returns false while the window is minimised.
  std::expected<bool, Error> recreateSwapchain();
  std::expected<void, Error> render();
  void record(VkCommandBuffer cmd, std::uint32_t imageIndex) const;
};

Renderer::Impl::~Impl() {
  if (!context) {
    return;
  }
  const VkDevice device = context->device();
  vkDeviceWaitIdle(device);
  destroyRenderFinishedSemaphores();
  for (FrameData &frame : frames) {
    vkDestroyFence(device, frame.inFlight, nullptr);
    vkDestroySemaphore(device, frame.imageAvailable, nullptr);
    vkDestroyCommandPool(device, frame.commandPool, nullptr);
  }
}

std::expected<void, Error> Renderer::Impl::init() {
  auto createdContext = VulkanContext::create(window, config.vulkan);
  if (!createdContext) {
    return std::unexpected(createdContext.error());
  }
  context = std::move(*createdContext);

  auto createdSwapchain = Swapchain::create(*context, window);
  if (!createdSwapchain) {
    return std::unexpected(createdSwapchain.error());
  }
  swapchain = std::move(*createdSwapchain);

  if (auto frameResources = createFrameResources(); !frameResources) {
    return frameResources;
  }
  return createRenderFinishedSemaphores();
}

std::expected<void, Error> Renderer::Impl::createFrameResources() {
  const VkDevice device = context->device();
  for (FrameData &frame : frames) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context->graphicsQueueFamily();
    if (auto r = vkCheck(
            vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool),
            kCode, "vkCreateCommandPool");
        !r) {
      return r;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = frame.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (auto r = vkCheck(
            vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer),
            kCode, "vkAllocateCommandBuffers");
        !r) {
      return r;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (auto r = vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                           &frame.imageAvailable),
                         kCode, "vkCreateSemaphore");
        !r) {
      return r;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (auto r = vkCheck(
            vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight), kCode,
            "vkCreateFence");
        !r) {
      return r;
    }
  }
  return {};
}

std::expected<void, Error> Renderer::Impl::createRenderFinishedSemaphores() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  renderFinished.assign(swapchain->images().size(), VK_NULL_HANDLE);
  for (VkSemaphore &semaphore : renderFinished) {
    if (auto r = vkCheck(vkCreateSemaphore(context->device(), &semaphoreInfo,
                                           nullptr, &semaphore),
                         kCode, "vkCreateSemaphore");
        !r) {
      return r;
    }
  }
  return {};
}

void Renderer::Impl::destroyRenderFinishedSemaphores() {
  for (VkSemaphore semaphore : renderFinished) {
    vkDestroySemaphore(context->device(), semaphore, nullptr);
  }
  renderFinished.clear();
}

std::expected<bool, Error> Renderer::Impl::recreateSwapchain() {
  auto recreated = swapchain->recreate(window);
  if (!recreated || !*recreated) {
    return recreated;
  }
  // recreate() waited for the device to be idle, so the semaphores are unused.
  destroyRenderFinishedSemaphores();
  if (auto r = createRenderFinishedSemaphores(); !r) {
    return std::unexpected(r.error());
  }
  return true;
}

void Renderer::Impl::record(VkCommandBuffer cmd,
                            std::uint32_t imageIndex) const {
  const VkImage image = swapchain->images()[imageIndex];

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  transitionImage(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = swapchain->imageViews()[imageIndex];
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = {{config.clearColor.r, config.clearColor.g,
                                       config.clearColor.b,
                                       config.clearColor.a}};

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {{0, 0}, swapchain->extent()};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;

  vkCmdBeginRendering(cmd, &renderingInfo);
  // Draw calls go here.
  vkCmdEndRendering(cmd);

  transitionImage(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, 0);

  vkEndCommandBuffer(cmd);
}

std::expected<void, Error> Renderer::Impl::render() {
  const int framebufferWidth = window.framebufferWidth();
  const int framebufferHeight = window.framebufferHeight();
  if (framebufferWidth == 0 || framebufferHeight == 0) {
    return {};
  }

  const VkExtent2D extent = swapchain->extent();
  if (framebufferWidth != static_cast<int>(extent.width) ||
      framebufferHeight != static_cast<int>(extent.height)) {
    auto recreated = recreateSwapchain();
    if (!recreated) {
      return std::unexpected(recreated.error());
    }
    if (!*recreated) {
      return {};
    }
  }

  const VkDevice device = context->device();
  FrameData &frame = frames[frameIndex];

  if (auto r = vkCheck(
          vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
          kCode, "vkWaitForFences");
      !r) {
    return r;
  }

  std::uint32_t imageIndex = 0;
  const VkResult acquired =
      vkAcquireNextImageKHR(device, swapchain->handle(), UINT64_MAX,
                            frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    auto recreated = recreateSwapchain();
    if (!recreated) {
      return std::unexpected(recreated.error());
    }
    return {};
  }
  if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
    return vkCheck(acquired, kCode, "vkAcquireNextImageKHR");
  }

  if (auto r = vkCheck(vkResetFences(device, 1, &frame.inFlight), kCode,
                       "vkResetFences");
      !r) {
    return r;
  }
  if (auto r = vkCheck(vkResetCommandPool(device, frame.commandPool, 0), kCode,
                       "vkResetCommandPool");
      !r) {
    return r;
  }

  record(frame.commandBuffer, imageIndex);

  VkSemaphoreSubmitInfo waitInfo{};
  waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  waitInfo.semaphore = frame.imageAvailable;
  waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signalInfo{};
  signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signalInfo.semaphore = renderFinished[imageIndex];
  signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

  VkCommandBufferSubmitInfo commandInfo{};
  commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commandInfo.commandBuffer = frame.commandBuffer;

  VkSubmitInfo2 submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submitInfo.waitSemaphoreInfoCount = 1;
  submitInfo.pWaitSemaphoreInfos = &waitInfo;
  submitInfo.commandBufferInfoCount = 1;
  submitInfo.pCommandBufferInfos = &commandInfo;
  submitInfo.signalSemaphoreInfoCount = 1;
  submitInfo.pSignalSemaphoreInfos = &signalInfo;
  if (auto r = vkCheck(
          vkQueueSubmit2(context->graphicsQueue(), 1, &submitInfo,
                         frame.inFlight),
          kCode, "vkQueueSubmit2");
      !r) {
    return r;
  }

  const VkSwapchainKHR swapchainHandle = swapchain->handle();
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinished[imageIndex];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchainHandle;
  presentInfo.pImageIndices = &imageIndex;
  const VkResult presented =
      vkQueuePresentKHR(context->presentQueue(), &presentInfo);
  frameIndex = (frameIndex + 1) % kFramesInFlight;

  if (presented == VK_ERROR_OUT_OF_DATE_KHR) {
    auto recreated = recreateSwapchain();
    if (!recreated) {
      return std::unexpected(recreated.error());
    }
    return {};
  }
  // A suboptimal swapchain is ignored; a size mismatch is caught at the start
  // of the next frame.
  if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) {
    return vkCheck(presented, kCode, "vkQueuePresentKHR");
  }
  return {};
}

Renderer::Renderer(std::unique_ptr<Impl> impl) : m_pImpl(std::move(impl)) {}

Renderer::~Renderer() = default;

std::expected<std::unique_ptr<Renderer>, Error>
Renderer::create(const Window &window, const RendererConfig &config) {
  auto impl = std::make_unique<Impl>(window, config);
  if (auto initialised = impl->init(); !initialised) {
    return std::unexpected(initialised.error());
  }
  return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

std::expected<void, Error> Renderer::render() { return m_pImpl->render(); }
} // namespace Paper
