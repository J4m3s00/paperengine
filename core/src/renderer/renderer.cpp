#include "core/renderer/renderer.h"

#include "core/renderer/shader.h"
#include "core/scene/scene.h"
#include "core/swapchain.h"
#include "core/vulkan_context.h"
#include "core/window.h"

#include "gpu_resources.h"
#include "vk_check.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace Paper {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr ErrorCode kCode = ErrorCode::Vulkan;

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// Set 0, binding 0 (see ADR 0004). Matches `FrameData` in mesh.vert.
struct FrameUniforms {
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 viewProj;
  glm::vec4 cameraPosition;
};

// Per-object push constants. Matches `Push` in mesh.vert.
struct PushConstants {
  glm::mat4 model;
  glm::mat4 normalMatrix;
};
static_assert(sizeof(PushConstants) == 128);

struct FrameData {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkFence inFlight = VK_NULL_HANDLE;
  GpuBuffer uniforms;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct MeshResource {
  GpuBuffer vertexBuffer;
  GpuBuffer indexBuffer;
  std::uint32_t indexCount = 0;
  std::uint32_t generation = 0;
  bool alive = false;
};

struct DrawItem {
  const MeshResource *mesh;
  PushConstants push;
};

std::filesystem::path executableDir() {
#ifdef __APPLE__
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return std::filesystem::path(buffer.data()).parent_path();
  }
#endif
  return {};
}

std::filesystem::path resolveAssetRoot(const std::filesystem::path &configured) {
  if (!configured.empty()) {
    return configured;
  }
  const std::filesystem::path exeDir = executableDir();
  if (!exeDir.empty() && std::filesystem::exists(exeDir / "shaders")) {
    return exeDir;
  }
  return PAPER_DEFAULT_ASSET_ROOT;
}

VkShaderStageFlagBits toVk(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return VK_SHADER_STAGE_VERTEX_BIT;
  case ShaderStage::Fragment:
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  return VK_SHADER_STAGE_VERTEX_BIT;
}

void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageAspectFlags aspect, VkImageLayout from,
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
  barrier.subresourceRange = {aspect, 0, 1, 0, 1};

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
  GpuImage depth;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  // Temporary single pipeline until materials exist (issue "Material system").
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  std::vector<MeshResource> meshes;
  std::vector<std::uint32_t> freeMeshSlots;
  std::vector<DrawItem> drawList;

  Impl(const Window &w, RendererConfig c) : window(w), config(std::move(c)) {}
  ~Impl();

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;

  std::expected<void, Error> init();
  std::expected<void, Error> createFrameResources();
  std::expected<void, Error> createRenderFinishedSemaphores();
  std::expected<void, Error> createDepth();
  std::expected<void, Error> createDescriptors();
  std::expected<void, Error> createPipeline();
  std::expected<MeshHandle, Error> createMesh(const MeshData &data);
  void destroyMesh(MeshHandle handle);
  MeshResource *findMesh(MeshHandle handle);
  void destroyRenderFinishedSemaphores();
  // Returns false while the window is minimised.
  std::expected<bool, Error> recreateSwapchain();
  std::expected<void, Error> render(const Scene &scene, const Camera &camera);
  void record(VkCommandBuffer cmd, const FrameData &frame,
              std::uint32_t imageIndex) const;
};

Renderer::Impl::~Impl() {
  if (!context) {
    return;
  }
  const VkDevice device = context->device();
  vkDeviceWaitIdle(device);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
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
  if (auto semaphores = createRenderFinishedSemaphores(); !semaphores) {
    return semaphores;
  }
  if (auto depthTarget = createDepth(); !depthTarget) {
    return depthTarget;
  }
  if (auto descriptors = createDescriptors(); !descriptors) {
    return descriptors;
  }
  return createPipeline();
}

std::expected<void, Error> Renderer::Impl::createDepth() {
  auto created = GpuImage::create(
      *context, swapchain->extent(), kDepthFormat,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
  if (!created) {
    return std::unexpected(created.error());
  }
  depth = std::move(*created);
  return {};
}

std::expected<void, Error> Renderer::Impl::createDescriptors() {
  const VkDevice device = context->device();

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &binding;
  if (auto r = vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                                   &descriptorSetLayout),
                       kCode, "vkCreateDescriptorSetLayout");
      !r) {
    return r;
  }

  const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                      kFramesInFlight};
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets = kFramesInFlight;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  if (auto r = vkCheck(
          vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
          kCode, "vkCreateDescriptorPool");
      !r) {
    return r;
  }

  std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
  layouts.fill(descriptorSetLayout);
  std::array<VkDescriptorSet, kFramesInFlight> sets{};
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = kFramesInFlight;
  allocInfo.pSetLayouts = layouts.data();
  if (auto r = vkCheck(vkAllocateDescriptorSets(device, &allocInfo, sets.data()),
                       kCode, "vkAllocateDescriptorSets");
      !r) {
    return r;
  }

  for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
    auto uniforms = GpuBuffer::create(*context, sizeof(FrameUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    if (!uniforms) {
      return std::unexpected(uniforms.error());
    }
    frames[i].uniforms = std::move(*uniforms);
    frames[i].descriptorSet = sets[i];

    const VkDescriptorBufferInfo bufferInfo{frames[i].uniforms.handle(), 0,
                                            sizeof(FrameUniforms)};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = sets[i];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  return {};
}

std::expected<void, Error> Renderer::Impl::createPipeline() {
  const std::filesystem::path shaderDir =
      resolveAssetRoot(config.assetRoot) / "shaders";
  auto vertex = Shader::load(*context, shaderDir / "mesh.vert.spv",
                             ShaderStage::Vertex);
  if (!vertex) {
    return std::unexpected(vertex.error());
  }
  auto fragment = Shader::load(*context, shaderDir / "mesh.frag.spv",
                               ShaderStage::Fragment);
  if (!fragment) {
    return std::unexpected(fragment.error());
  }

  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  const std::array<const Shader *, 2> shaders{vertex->get(), fragment->get()};
  for (std::size_t i = 0; i < stages.size(); ++i) {
    stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[i].stage = toVk(shaders[i]->stage());
    stages[i].module = shaders[i]->module();
    stages[i].pName = "main";
  }

  const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0,
                                      sizeof(PushConstants)};
  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptorSetLayout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;
  if (auto r = vkCheck(vkCreatePipelineLayout(context->device(), &layoutInfo,
                                              nullptr, &pipelineLayout),
                       kCode, "vkCreatePipelineLayout");
      !r) {
    return r;
  }

  const VkVertexInputBindingDescription vertexBinding{
      0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  const std::array<VkVertexInputAttributeDescription, 4> vertexAttributes{{
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
  }};
  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &vertexBinding;
  vertexInput.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(vertexAttributes.size());
  vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // Right-handed, Y up: the negative-height viewport flips Y, so
  // counter-clockwise triangles are front faces (see CONTEXT.md).
  VkPipelineRasterizationStateCreateInfo rasterization{};
  rasterization.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0F;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.maxDepthBounds = 1.0F;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAttachment{};
  blendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &blendAttachment;

  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount =
      static_cast<std::uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  const VkFormat colorFormat = swapchain->format();
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = kDepthFormat;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = &rendering;
  pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterization;
  pipelineInfo.pMultisampleState = &multisample;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlend;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  return vkCheck(vkCreateGraphicsPipelines(context->device(), VK_NULL_HANDLE, 1,
                                           &pipelineInfo, nullptr,
                                           &pipeline),
                 kCode, "vkCreateGraphicsPipelines");
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
  if (auto r = createDepth(); !r) {
    return std::unexpected(r.error());
  }
  return true;
}

void Renderer::Impl::record(VkCommandBuffer cmd, const FrameData &frame,
                            std::uint32_t imageIndex) const {
  const VkImage image = swapchain->images()[imageIndex];

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  // The depth image is shared by all frames in flight; this barrier orders
  // this frame's depth use after the previous frame's.
  constexpr VkPipelineStageFlags2 kDepthStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  transitionImage(cmd, depth.image(), VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = swapchain->imageViews()[imageIndex];
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = {{config.clearColor.r, config.clearColor.g,
                                       config.clearColor.b,
                                       config.clearColor.a}};

  VkRenderingAttachmentInfo depthAttachment{};
  depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachment.imageView = depth.view();
  depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.clearValue.depthStencil = {1.0F, 0};

  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = {{0, 0}, swapchain->extent()};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = &depthAttachment;

  vkCmdBeginRendering(cmd, &renderingInfo);

  // Negative height flips Y so the clip space is Y up (VK_KHR_maintenance1).
  const VkExtent2D extent = swapchain->extent();
  const VkViewport viewport{0.0F,
                            static_cast<float>(extent.height),
                            static_cast<float>(extent.width),
                            -static_cast<float>(extent.height),
                            0.0F,
                            1.0F};
  const VkRect2D scissor{{0, 0}, extent};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                          0, 1, &frame.descriptorSet, 0, nullptr);

  for (const DrawItem &draw : drawList) {
    const VkBuffer vertexBuffer = draw.mesh->vertexBuffer.handle();
    const VkDeviceSize offset = 0;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushConstants), &draw.push);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, draw.mesh->indexBuffer.handle(), 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, draw.mesh->indexCount, 1, 0, 0, 0);
  }

  vkCmdEndRendering(cmd);

  transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, 0);

  vkEndCommandBuffer(cmd);
}

MeshResource *Renderer::Impl::findMesh(MeshHandle handle) {
  if (!handle.valid() || handle.index >= meshes.size()) {
    return nullptr;
  }
  MeshResource &mesh = meshes[handle.index];
  return mesh.alive && mesh.generation == handle.generation ? &mesh : nullptr;
}

std::expected<MeshHandle, Error>
Renderer::Impl::createMesh(const MeshData &data) {
  if (data.vertices.empty() || data.indices.empty()) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "createMesh: mesh has no vertices or indices"});
  }
  for (const std::uint32_t index : data.indices) {
    if (index >= data.vertices.size()) {
      return std::unexpected(Error{ErrorCode::InvalidArgument,
                                   "createMesh: index out of range"});
    }
  }

  auto vertexBuffer = uploadBuffer(
      *context, data.vertices.data(), data.vertices.size() * sizeof(Vertex),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  if (!vertexBuffer) {
    return std::unexpected(vertexBuffer.error());
  }
  auto indexBuffer = uploadBuffer(
      *context, data.indices.data(),
      data.indices.size() * sizeof(std::uint32_t),
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  if (!indexBuffer) {
    return std::unexpected(indexBuffer.error());
  }

  std::uint32_t slot = 0;
  if (!freeMeshSlots.empty()) {
    slot = freeMeshSlots.back();
    freeMeshSlots.pop_back();
  } else {
    slot = static_cast<std::uint32_t>(meshes.size());
    meshes.emplace_back();
  }
  MeshResource &mesh = meshes[slot];
  mesh.vertexBuffer = std::move(*vertexBuffer);
  mesh.indexBuffer = std::move(*indexBuffer);
  mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
  mesh.alive = true;
  return MeshHandle{slot, mesh.generation};
}

void Renderer::Impl::destroyMesh(MeshHandle handle) {
  MeshResource *mesh = findMesh(handle);
  assert(mesh != nullptr && "destroyMesh: stale or invalid handle");
  if (mesh == nullptr) {
    return;
  }
  vkDeviceWaitIdle(context->device());
  mesh->vertexBuffer = GpuBuffer();
  mesh->indexBuffer = GpuBuffer();
  mesh->indexCount = 0;
  mesh->alive = false;
  ++mesh->generation;
  freeMeshSlots.push_back(handle.index);
}

std::expected<void, Error> Renderer::Impl::render(const Scene &scene,
                                                   const Camera &camera) {
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

  // The fence guarantees the GPU is done with this frame's uniform buffer.
  const VkExtent2D currentExtent = swapchain->extent();
  FrameUniforms uniforms{};
  uniforms.view = camera.view();
  uniforms.proj = glm::perspectiveRH_ZO(
      camera.fovY,
      static_cast<float>(currentExtent.width) /
          static_cast<float>(currentExtent.height),
      camera.nearPlane, camera.farPlane);
  uniforms.viewProj = uniforms.proj * uniforms.view;
  uniforms.cameraPosition = glm::vec4(camera.position, 1.0F);
  std::memcpy(frame.uniforms.mapped(), &uniforms, sizeof(uniforms));
  frame.uniforms.flush();

  drawList.clear();
  for (const auto [entity, world, renderer] :
       scene.registry()
           .view<const WorldTransform, const MeshRenderer>()
           .each()) {
    (void)entity;
    const MeshResource *mesh = findMesh(renderer.mesh);
    assert(mesh != nullptr && "MeshRenderer references a stale mesh handle");
    if (mesh == nullptr) {
      continue;
    }
    drawList.push_back(
        {mesh, {world.matrix, glm::transpose(glm::inverse(world.matrix))}});
  }

  record(frame.commandBuffer, frame, imageIndex);

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

std::expected<MeshHandle, Error> Renderer::createMesh(const MeshData &data) {
  return m_pImpl->createMesh(data);
}

void Renderer::destroyMesh(MeshHandle mesh) { m_pImpl->destroyMesh(mesh); }

std::expected<void, Error> Renderer::render(const Scene &scene,
                                            const Camera &camera) {
  return m_pImpl->render(scene, camera);
}
} // namespace Paper
