#include "base/paths.h"
#include "base/profiler.h"
#include "core/renderer/renderer.h"

#include "core/renderer/shader.h"
#include "core/scene/scene.h"
#include "core/swapchain.h"
#include "core/vulkan_context.h"
#include "core/window.h"

#include "assets/gltf_loader.h"
#include "gpu_resources.h"
#include "imgui_layer.h"
#include "pipeline.h"
#include "resource_pool.h"
#include "shader_reflection.h"
#include "vk_check.h"

#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Paper {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr ErrorCode kCode = ErrorCode::Vulkan;

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// Material descriptor sets live in one pool: kMaxMaterials * frames in flight.
constexpr std::uint32_t kMaxMaterials = 1024;
constexpr std::uint32_t kMaxTextureSlots = 8; // Per shader (set 1 bindings).
constexpr float kMaxAnisotropy = 4.0F;

constexpr std::size_t kMaxLights = 8;

// Matches `Light` in shaders/include/frame.glsl.
struct GpuLight {
  glm::vec4 vector;         // xyz: direction of travel, w: 0 = directional.
  glm::vec4 colorIntensity; // rgb: color, a: intensity.
};

// Set 0, binding 0 (see ADR 0004). Matches `FrameData` in
// shaders/include/frame.glsl (std140).
struct FrameUniforms {
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 viewProj;
  glm::vec4 cameraPosition;
  glm::vec4 ambient;
  glm::ivec4 lightCount;
  std::array<GpuLight, kMaxLights> lights;
};
static_assert(sizeof(FrameUniforms) == 3 * 64 + 3 * 16 + kMaxLights * 32);

// Per-object push constants. Matches `Push` in pbr.vert.
struct PushConstants {
  glm::mat4 model;
  glm::mat4 normalMatrix;
};
static_assert(sizeof(PushConstants) == 128);

// Most joint matrices all skinned meshes of one frame may use together.
constexpr std::uint32_t kMaxJointMatrices = 8192;

struct FrameData {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkSemaphore imageAvailable = VK_NULL_HANDLE;
  VkFence inFlight = VK_NULL_HANDLE;
  GpuBuffer uniforms;
  // Set 0, binding 1: joint matrices of skinned meshes (host visible).
  GpuBuffer joints;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct MeshResource {
  GpuBuffer vertexBuffer;
  GpuBuffer indexBuffer;
  std::uint32_t indexCount = 0;
};

struct TextureResource {
  GpuImage image;
  VkSampler sampler = VK_NULL_HANDLE; // Owned by Renderer::Impl::samplers.
  std::uint32_t useCount = 0;         // Material slots referencing it.
  bool isDefault = false;
};

// Owns the pipeline layout and the set 1 layout of one shader.
struct ShaderResource {
  std::unique_ptr<Shader> vertex;
  std::unique_ptr<Shader> fragment;
  MaterialLayout layout;
  VkDevice device = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  std::uint32_t materialCount = 0;

  ShaderResource() = default;
  ShaderResource(ShaderResource &&other) noexcept
      : vertex(std::move(other.vertex)), fragment(std::move(other.fragment)),
        layout(std::move(other.layout)),
        device(std::exchange(other.device, VK_NULL_HANDLE)),
        materialSetLayout(
            std::exchange(other.materialSetLayout, VK_NULL_HANDLE)),
        pipelineLayout(std::exchange(other.pipelineLayout, VK_NULL_HANDLE)),
        materialCount(other.materialCount) {}
  ShaderResource(const ShaderResource &) = delete;
  ShaderResource &operator=(const ShaderResource &) = delete;
  ShaderResource &operator=(ShaderResource &&) = delete;
  ~ShaderResource() {
    if (device != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      vkDestroyDescriptorSetLayout(device, materialSetLayout, nullptr);
    }
  }
};

// CPU copy of the parameter block plus one UBO and descriptor set per frame in
// flight (ADR 0003). `dirtyFrames` has a bit per frame whose UBO / texture
// descriptors are stale.
struct MaterialResource {
  ShaderHandle shader;
  RenderState state;
  std::vector<std::uint8_t> block;
  std::array<GpuBuffer, kFramesInFlight> uniforms;
  std::vector<TextureHandle> textures; // One per shader texture slot.
  std::array<VkDescriptorSet, kFramesInFlight> sets{};
  std::uint32_t dirtyFrames = 0;
  VkDevice device = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;

  MaterialResource() = default;
  MaterialResource(MaterialResource &&other) noexcept
      : shader(other.shader), state(other.state),
        block(std::move(other.block)), uniforms(std::move(other.uniforms)),
        textures(std::move(other.textures)),
        sets(other.sets), dirtyFrames(other.dirtyFrames),
        device(other.device), pool(std::exchange(other.pool, VK_NULL_HANDLE)) {}
  MaterialResource(const MaterialResource &) = delete;
  MaterialResource &operator=(const MaterialResource &) = delete;
  MaterialResource &operator=(MaterialResource &&) = delete;
  ~MaterialResource() {
    if (pool != VK_NULL_HANDLE) {
      for (const VkDescriptorSet set : sets) {
        if (set != VK_NULL_HANDLE) {
          vkFreeDescriptorSets(device, pool, 1, &set);
        }
      }
    }
  }
};

struct PipelineKey {
  std::uint32_t shaderIndex;
  std::uint32_t shaderGeneration;
  RenderState state;
  VkFormat colorFormat;
  VkFormat depthFormat;

  friend auto operator<=>(const PipelineKey &, const PipelineKey &) = default;
};

struct DrawItem {
  const MeshResource *mesh;
  const MaterialResource *material;
  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  PushConstants push;
  // Skinned meshes: index of the first joint matrix in the palette.
  std::uint32_t firstJoint = 0;
};

// Runs a callback when destroyed; used for raw Vulkan objects in the deletion
// queue.
struct Deleter {
  std::function<void()> destroy;

  Deleter() = default;
  Deleter(const Deleter &) = delete;
  Deleter &operator=(const Deleter &) = delete;
  ~Deleter() {
    if (destroy) {
      destroy();
    }
  }
};

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

std::uintptr_t sortKey(const void *pointer) {
  return reinterpret_cast<std::uintptr_t>(pointer);
}
} // namespace

struct Renderer::Impl {
  const Window &window;
  RendererConfig config;
  std::filesystem::path assetRoot;
  std::unique_ptr<VulkanContext> context;
  std::unique_ptr<Swapchain> swapchain;
  std::array<FrameData, kFramesInFlight> frames{};
  // One per swapchain image: presentation may still hold the semaphore after
  // the frame that signalled it has finished.
  std::vector<VkSemaphore> renderFinished;
  std::uint32_t frameIndex = 0;
  // Number of frames submitted so far; drives the deletion queue.
  std::uint64_t frameNumber = 0;
  GpuImage depth;
  // Dear ImGui backend; destroyed first, while the device is still alive.
  std::unique_ptr<ImGuiLayer> imguiLayer;
  std::function<void()> uiCallback;
  // True when this frame's UI draw data should be recorded.
  bool uiFrameReady = false;
  // Set 0 (per frame).
  VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool frameDescriptorPool = VK_NULL_HANDLE;
  // Set 1 (per material).
  VkDescriptorPool materialDescriptorPool = VK_NULL_HANDLE;

  ResourcePool<MeshResource, MeshHandle> meshes;
  ResourcePool<TextureResource, TextureHandle> textures;
  ResourcePool<Model, ModelHandle> models;
  std::map<SamplerSettings, VkSampler> samplers;
  std::array<TextureHandle, 2> defaultTextures{};
  ResourcePool<ShaderResource, ShaderHandle> shaders;
  ResourcePool<MaterialResource, MaterialHandle> materials;
  std::map<PipelineKey, VkPipeline> pipelines;

  struct Pending {
    std::uint64_t retireFrame;
    std::shared_ptr<void> holder;
  };
  std::deque<Pending> pending;
  std::vector<DrawItem> drawList;
  bool warnedAboutLightLimit = false;
  // Screenshot: requested by the user, captured by the next render().
  std::optional<std::filesystem::path> screenshotPath;
  GpuBuffer screenshotBuffer;

  Impl(const Window &w, RendererConfig c)
      : window(w), config(std::move(c)),
        assetRoot(resolveAssetRoot(config.assetRoot)) {}
  ~Impl();

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;

  std::expected<void, Error> init();
  std::expected<void, Error> createFrameResources();
  std::expected<void, Error> createRenderFinishedSemaphores();
  void destroyRenderFinishedSemaphores();
  std::expected<void, Error> createDepth();
  std::expected<void, Error> createDescriptors();
  // Returns false while the window is minimised.
  std::expected<bool, Error> recreateSwapchain();

  std::expected<MeshHandle, Error> createMesh(const MeshData &data);
  void destroyMesh(MeshHandle handle);
  std::expected<VkSampler, Error> getSampler(const SamplerSettings &settings);
  std::expected<void, Error> createDefaultTextures();
  std::expected<TextureHandle, Error>
  createTexture(const Image &image, ColorSpace colorSpace,
                const SamplerSettings &sampler);
  void destroyTexture(TextureHandle handle);
  std::expected<void, Error> setMaterialTexture(MaterialHandle handle,
                                                std::string_view slot,
                                                TextureHandle texture);
  std::expected<ShaderHandle, Error> createShader(const ShaderSource &source);
  void destroyShader(ShaderHandle handle);
  std::expected<MaterialHandle, Error> createMaterial(ShaderHandle shader,
                                                      const RenderState &state);
  void destroyMaterial(MaterialHandle handle);
  std::expected<void, Error> setMaterialParam(MaterialHandle handle,
                                              std::string_view name,
                                              const MaterialParam &value);
  std::expected<VkPipeline, Error> getPipeline(ShaderHandle handle,
                                               const ShaderResource &shader,
                                               const RenderState &state);

  // Deletion queue (ADR 0003): resources freed by the user are destroyed once
  // the frames that may still use them have completed.
  void retire(std::shared_ptr<void> holder) {
    pending.push_back({frameNumber + 1, std::move(holder)});
  }
  void retireCall(std::function<void()> destroy) {
    auto deleter = std::make_shared<Deleter>();
    deleter->destroy = std::move(destroy);
    retire(std::move(deleter));
  }
  void collectGarbage() {
    while (!pending.empty() && pending.front().retireFrame <= frameNumber) {
      pending.pop_front();
    }
  }

  // Brings the UBO and texture descriptors of this frame's copy up to date.
  void updateMaterial(MaterialResource &material, const ShaderResource &shader);
  std::expected<void, Error> saveScreenshot();
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
  imguiLayer.reset();

  // Order matters: resources that free descriptor sets / layouts must go
  // before the pools and the device-level objects below.
  pending.clear();
  models.clear(); // Plain handle lists; the resources die with the pools below.
  materials.clear();
  shaders.clear();
  meshes.clear();
  textures.clear();
  for (const auto &[settings, sampler] : samplers) {
    vkDestroySampler(device, sampler, nullptr);
  }
  samplers.clear();
  for (const auto &[key, pipeline] : pipelines) {
    vkDestroyPipeline(device, pipeline, nullptr);
  }
  pipelines.clear();

  vkDestroyDescriptorPool(device, materialDescriptorPool, nullptr);
  vkDestroyDescriptorPool(device, frameDescriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, frameSetLayout, nullptr);
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
  if (auto textures = createDefaultTextures(); !textures) {
    return textures;
  }
  auto createdLayer =
      ImGuiLayer::create(window, *context, swapchain->format(), kDepthFormat,
                         static_cast<std::uint32_t>(swapchain->images().size()));
  if (!createdLayer) {
    return std::unexpected(createdLayer.error());
  }
  imguiLayer = std::move(*createdLayer);
  return {};
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

  std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  if (auto r = vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                                   &frameSetLayout),
                       kCode, "vkCreateDescriptorSetLayout");
      !r) {
    return r;
  }

  const std::array<VkDescriptorPoolSize, 2> framePoolSizes{{
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight},
  }};
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets = kFramesInFlight;
  poolInfo.poolSizeCount = static_cast<std::uint32_t>(framePoolSizes.size());
  poolInfo.pPoolSizes = framePoolSizes.data();
  if (auto r = vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr,
                                              &frameDescriptorPool),
                       kCode, "vkCreateDescriptorPool");
      !r) {
    return r;
  }

  const std::array<VkDescriptorPoolSize, 2> materialPoolSizes{{
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxMaterials * kFramesInFlight},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       kMaxMaterials * kFramesInFlight * kMaxTextureSlots},
  }};
  VkDescriptorPoolCreateInfo materialPoolInfo{};
  materialPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  materialPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  materialPoolInfo.maxSets = kMaxMaterials * kFramesInFlight;
  materialPoolInfo.poolSizeCount =
      static_cast<std::uint32_t>(materialPoolSizes.size());
  materialPoolInfo.pPoolSizes = materialPoolSizes.data();
  if (auto r = vkCheck(vkCreateDescriptorPool(device, &materialPoolInfo,
                                              nullptr, &materialDescriptorPool),
                       kCode, "vkCreateDescriptorPool");
      !r) {
    return r;
  }

  std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
  layouts.fill(frameSetLayout);
  std::array<VkDescriptorSet, kFramesInFlight> sets{};
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = frameDescriptorPool;
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
    auto joints = GpuBuffer::create(*context, kMaxJointMatrices * sizeof(glm::mat4),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    if (!joints) {
      return std::unexpected(joints.error());
    }
    frames[i].joints = std::move(*joints);
    const VkDescriptorBufferInfo jointInfo{frames[i].joints.handle(), 0,
                                           kMaxJointMatrices * sizeof(glm::mat4)};
    VkWriteDescriptorSet writes[2] = {write, write};
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &jointInfo;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
  }
  return {};
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

  MeshResource mesh;
  mesh.vertexBuffer = std::move(*vertexBuffer);
  mesh.indexBuffer = std::move(*indexBuffer);
  mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
  return meshes.create(std::move(mesh));
}

void Renderer::Impl::destroyMesh(MeshHandle handle) {
  auto mesh = meshes.destroy(handle);
  assert(mesh.has_value() && "destroyMesh: stale or invalid handle");
  if (mesh) {
    retire(std::make_shared<MeshResource>(std::move(*mesh)));
  }
}

std::expected<ShaderHandle, Error>
Renderer::Impl::createShader(const ShaderSource &source) {
  const std::filesystem::path shaderDir = assetRoot / "shaders";
  const auto resolve = [&](const std::filesystem::path &path) {
    return path.is_absolute() ? path : shaderDir / path;
  };

  ShaderResource shader;
  auto vertex = Shader::load(*context, resolve(source.vertex),
                             ShaderStage::Vertex);
  if (!vertex) {
    return std::unexpected(vertex.error());
  }
  shader.vertex = std::move(*vertex);
  auto fragment = Shader::load(*context, resolve(source.fragment),
                               ShaderStage::Fragment);
  if (!fragment) {
    return std::unexpected(fragment.error());
  }
  shader.fragment = std::move(*fragment);

  auto layout = reflectMaterialLayout(shader.vertex->spirv(),
                                      shader.fragment->spirv());
  if (!layout) {
    return std::unexpected(layout.error());
  }
  shader.layout = std::move(*layout);

  const VkDevice device = context->device();

  if (shader.layout.textures.size() > kMaxTextureSlots) {
    return std::unexpected(Error{ErrorCode::Shader,
                                 "Shader has more than " +
                                     std::to_string(kMaxTextureSlots) +
                                     " texture slots"});
  }

  // Set 1: the uniform block (if any) plus one sampler per texture slot. A
  // shader without either gets an empty layout and its set stays unbound.
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  constexpr VkShaderStageFlags kStages =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  if (shader.layout.blockSize > 0) {
    bindings.push_back({0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kStages,
                        nullptr});
  }
  for (const TextureSlotInfo &slot : shader.layout.textures) {
    bindings.push_back({slot.binding,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, kStages,
                        nullptr});
  }
  VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
  setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
  setLayoutInfo.pBindings = bindings.data();
  if (auto r = vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo,
                                                   nullptr,
                                                   &shader.materialSetLayout),
                       kCode, "vkCreateDescriptorSetLayout");
      !r) {
    return std::unexpected(r.error());
  }
  shader.device = device; // From here on the resource cleans up after itself.

  const std::array<VkDescriptorSetLayout, 2> setLayouts{frameSetLayout,
                                                        shader.materialSetLayout};
  const VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0,
                                      sizeof(PushConstants)};
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<std::uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (auto r = vkCheck(vkCreatePipelineLayout(device, &pipelineLayoutInfo,
                                              nullptr, &shader.pipelineLayout),
                       kCode, "vkCreatePipelineLayout");
      !r) {
    return std::unexpected(r.error());
  }
  return shaders.create(std::move(shader));
}

void Renderer::Impl::destroyShader(ShaderHandle handle) {
  ShaderResource *shader = shaders.get(handle);
  assert(shader != nullptr && "destroyShader: stale or invalid handle");
  if (shader == nullptr) {
    return;
  }
  assert(shader->materialCount == 0 &&
         "destroyShader: shader is still used by a material");
  if (shader->materialCount > 0) {
    spdlog::error("destroyShader: {} material(s) still use this shader",
                  shader->materialCount);
    return;
  }

  for (auto it = pipelines.begin(); it != pipelines.end();) {
    if (it->first.shaderIndex == handle.index &&
        it->first.shaderGeneration == handle.generation) {
      const VkDevice device = context->device();
      const VkPipeline pipeline = it->second;
      retireCall([device, pipeline] { vkDestroyPipeline(device, pipeline, nullptr); });
      it = pipelines.erase(it);
    } else {
      ++it;
    }
  }
  auto taken = shaders.destroy(handle);
  retire(std::make_shared<ShaderResource>(std::move(*taken)));
}

std::expected<MaterialHandle, Error>
Renderer::Impl::createMaterial(ShaderHandle shaderHandle,
                               const RenderState &state) {
  ShaderResource *shader = shaders.get(shaderHandle);
  if (shader == nullptr) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "createMaterial: invalid shader handle"});
  }

  const VkDevice device = context->device();
  MaterialResource material;
  material.shader = shaderHandle;
  material.state = state;
  material.block.assign(shader->layout.blockSize, 0);
  material.device = device;
  material.pool = materialDescriptorPool;
  material.dirtyFrames = (1U << kFramesInFlight) - 1U;

  // Unassigned texture slots point at a default texture.
  for (const TextureSlotInfo &slot : shader->layout.textures) {
    std::string lower = slot.name;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    const DefaultTexture which = lower.find("normal") != std::string::npos
                                     ? DefaultTexture::FlatNormal
                                     : DefaultTexture::White;
    const TextureHandle texture = defaultTextures[static_cast<std::size_t>(which)];
    ++textures.get(texture)->useCount;
    material.textures.push_back(texture);
  }
  // Undo the use counts if creation fails below.
  const auto releaseTextures = [&] {
    for (const TextureHandle texture : material.textures) {
      if (TextureResource *resource = textures.get(texture)) {
        --resource->useCount;
      }
    }
  };

  const bool hasSet = shader->layout.blockSize > 0 ||
                      !shader->layout.textures.empty();
  if (hasSet) {
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (shader->layout.blockSize > 0) {
        auto uniforms = GpuBuffer::create(
            *context, shader->layout.blockSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        if (!uniforms) {
          releaseTextures();
          return std::unexpected(uniforms.error());
        }
        material.uniforms[i] = std::move(*uniforms);
        std::memset(material.uniforms[i].mapped(), 0,
                    shader->layout.blockSize);
      }

      VkDescriptorSetAllocateInfo allocInfo{};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = materialDescriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &shader->materialSetLayout;
      if (auto r = vkCheck(
              vkAllocateDescriptorSets(device, &allocInfo, &material.sets[i]),
              kCode, "vkAllocateDescriptorSets (too many materials?)");
          !r) {
        releaseTextures();
        return std::unexpected(r.error());
      }

      if (shader->layout.blockSize > 0) {
        // Static; the texture descriptors are written in updateMaterial().
        const VkDescriptorBufferInfo bufferInfo{material.uniforms[i].handle(), 0,
                                                shader->layout.blockSize};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = material.sets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
      }
    }
  }

  ++shader->materialCount;
  return materials.create(std::move(material));
}

void Renderer::Impl::destroyMaterial(MaterialHandle handle) {
  auto material = materials.destroy(handle);
  assert(material.has_value() && "destroyMaterial: stale or invalid handle");
  if (!material) {
    return;
  }
  if (ShaderResource *shader = shaders.get(material->shader)) {
    --shader->materialCount;
  }
  for (const TextureHandle texture : material->textures) {
    if (TextureResource *resource = textures.get(texture)) {
      --resource->useCount;
    }
  }
  retire(std::make_shared<MaterialResource>(std::move(*material)));
}

std::expected<void, Error>
Renderer::Impl::setMaterialParam(MaterialHandle handle, std::string_view name,
                                 const MaterialParam &value) {
  MaterialResource *material = materials.get(handle);
  if (material == nullptr) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "setMaterialParam: invalid material handle"});
  }
  const ShaderResource *shader = shaders.get(material->shader);
  assert(shader != nullptr);
  const ParamInfo *param = shader->layout.find(name);
  if (param == nullptr) {
    std::string known;
    for (const ParamInfo &p : shader->layout.params) {
      known += (known.empty() ? "" : ", ") + p.name;
    }
    return std::unexpected(Error{
        ErrorCode::InvalidArgument,
        "Unknown material parameter '" + std::string(name) +
            "' (shader parameters: " + (known.empty() ? "none" : known) + ")"});
  }

  const auto write = [&](ParamType expected, const void *data) {
    if (param->type != expected) {
      return std::expected<void, Error>(std::unexpected(Error{
          ErrorCode::InvalidArgument,
          "Material parameter '" + std::string(name) + "' is a " +
              paramTypeName(param->type) + ", got a " +
              paramTypeName(expected)}));
    }
    std::memcpy(material->block.data() + param->offset, data,
                paramSize(expected));
    return std::expected<void, Error>();
  };

  const auto result = std::visit(
      [&](const auto &v) -> std::expected<void, Error> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          return write(ParamType::Float, &v);
        } else if constexpr (std::is_same_v<T, int>) {
          return write(ParamType::Int, &v);
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
          return write(ParamType::Vec2, &v);
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
          return write(ParamType::Vec3, &v);
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
          return write(ParamType::Vec4, &v);
        } else {
          return write(ParamType::Mat4, &v);
        }
      },
      value);
  if (result) {
    material->dirtyFrames = (1U << kFramesInFlight) - 1U;
  }
  return result;
}

std::expected<VkSampler, Error>
Renderer::Impl::getSampler(const SamplerSettings &settings) {
  if (const auto it = samplers.find(settings); it != samplers.end()) {
    return it->second;
  }
  const auto toFilter = [](Filter filter) {
    return filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  };
  const auto toWrap = [](WrapMode mode) {
    switch (mode) {
    case WrapMode::Repeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case WrapMode::MirroredRepeat:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case WrapMode::ClampToEdge:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  };

  VkSamplerCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.magFilter = toFilter(settings.magFilter);
  info.minFilter = toFilter(settings.minFilter);
  info.mipmapMode = settings.minFilter == Filter::Linear
                        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = toWrap(settings.wrapU);
  info.addressModeV = toWrap(settings.wrapV);
  info.addressModeW = toWrap(settings.wrapU);
  info.anisotropyEnable = VK_TRUE;
  info.maxAnisotropy = std::min(kMaxAnisotropy, context->maxSamplerAnisotropy());
  info.maxLod = VK_LOD_CLAMP_NONE;

  VkSampler sampler = VK_NULL_HANDLE;
  if (auto r = vkCheck(vkCreateSampler(context->device(), &info, nullptr, &sampler),
                       kCode, "vkCreateSampler");
      !r) {
    return std::unexpected(r.error());
  }
  samplers.emplace(settings, sampler);
  return sampler;
}

std::expected<TextureHandle, Error>
Renderer::Impl::createTexture(const Image &image, ColorSpace colorSpace,
                              const SamplerSettings &settings) {
  if (image.width == 0 || image.height == 0 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::unexpected(
        Error{ErrorCode::InvalidArgument,
              "createTexture: image must be non-empty tightly packed RGBA8"});
  }
  auto sampler = getSampler(settings);
  if (!sampler) {
    return std::unexpected(sampler.error());
  }
  const VkFormat format = colorSpace == ColorSpace::Srgb
                              ? VK_FORMAT_R8G8B8A8_SRGB
                              : VK_FORMAT_R8G8B8A8_UNORM;
  auto uploaded = uploadTexture(*context, image.pixels.data(), image.width,
                                image.height, format);
  if (!uploaded) {
    return std::unexpected(uploaded.error());
  }
  TextureResource texture;
  texture.image = std::move(*uploaded);
  texture.sampler = *sampler;
  return textures.create(std::move(texture));
}

std::expected<void, Error> Renderer::Impl::createDefaultTextures() {
  struct Spec {
    DefaultTexture which;
    std::array<std::uint8_t, 4> rgba;
    ColorSpace colorSpace;
  };
  const std::array<Spec, 2> specs{{
      {DefaultTexture::White, {255, 255, 255, 255}, ColorSpace::Srgb},
      // (0.5, 0.5, 1.0): the tangent-space normal pointing straight out.
      {DefaultTexture::FlatNormal, {128, 128, 255, 255}, ColorSpace::Linear},
  }};
  for (const Spec &spec : specs) {
    Image image{1, 1, {spec.rgba.begin(), spec.rgba.end()}};
    auto texture = createTexture(image, spec.colorSpace, {});
    if (!texture) {
      return std::unexpected(texture.error());
    }
    textures.get(*texture)->isDefault = true;
    defaultTextures[static_cast<std::size_t>(spec.which)] = *texture;
  }
  return {};
}

void Renderer::Impl::destroyTexture(TextureHandle handle) {
  TextureResource *texture = textures.get(handle);
  assert(texture != nullptr && "destroyTexture: stale or invalid handle");
  if (texture == nullptr) {
    return;
  }
  assert(!texture->isDefault && "destroyTexture: default textures are fixed");
  assert(texture->useCount == 0 &&
         "destroyTexture: texture is still used by a material");
  if (texture->isDefault || texture->useCount > 0) {
    spdlog::error("destroyTexture: texture is {}",
                  texture->isDefault ? "a default texture"
                                     : "still used by a material");
    return;
  }
  auto taken = textures.destroy(handle);
  retire(std::make_shared<TextureResource>(std::move(*taken)));
}

std::expected<void, Error>
Renderer::Impl::setMaterialTexture(MaterialHandle handle, std::string_view slot,
                                   TextureHandle texture) {
  MaterialResource *material = materials.get(handle);
  if (material == nullptr) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "setMaterialTexture: invalid material handle"});
  }
  TextureResource *newTexture = textures.get(texture);
  if (newTexture == nullptr) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "setMaterialTexture: invalid texture handle"});
  }
  const ShaderResource *shader = shaders.get(material->shader);
  assert(shader != nullptr);
  const int index = shader->layout.findTexture(slot);
  if (index < 0) {
    std::string known;
    for (const TextureSlotInfo &s : shader->layout.textures) {
      known += (known.empty() ? "" : ", ") + s.name;
    }
    return std::unexpected(Error{
        ErrorCode::InvalidArgument,
        "Unknown texture slot '" + std::string(slot) + "' (shader slots: " +
            (known.empty() ? "none" : known) + ")"});
  }

  TextureHandle &current = material->textures[static_cast<std::size_t>(index)];
  if (TextureResource *old = textures.get(current)) {
    --old->useCount;
  }
  ++newTexture->useCount;
  current = texture;
  material->dirtyFrames = (1U << kFramesInFlight) - 1U;
  return {};
}

std::expected<VkPipeline, Error>
Renderer::Impl::getPipeline(ShaderHandle handle, const ShaderResource &shader,
                            const RenderState &state) {
  const PipelineKey key{handle.index, handle.generation, state,
                        swapchain->format(), kDepthFormat};
  if (const auto it = pipelines.find(key); it != pipelines.end()) {
    return it->second;
  }
  auto pipeline = createGraphicsPipeline({context->device(),
                                          shader.pipelineLayout,
                                          shader.vertex->module(),
                                          shader.fragment->module(), state,
                                          key.colorFormat, key.depthFormat,
                                          shader.layout.skinned});
  if (!pipeline) {
    return std::unexpected(pipeline.error());
  }
  pipelines.emplace(key, *pipeline);
  return *pipeline;
}

void Renderer::Impl::updateMaterial(MaterialResource &material,
                                    const ShaderResource &shader) {
  const std::uint32_t bit = 1U << frameIndex;
  if ((material.dirtyFrames & bit) == 0) {
    return;
  }
  if (!material.block.empty()) {
    GpuBuffer &uniforms = material.uniforms[frameIndex];
    std::memcpy(uniforms.mapped(), material.block.data(), material.block.size());
    uniforms.flush();
  }
  if (!material.textures.empty()) {
    const std::size_t count = material.textures.size();
    std::vector<VkDescriptorImageInfo> imageInfos(count);
    std::vector<VkWriteDescriptorSet> writes(count);
    for (std::size_t i = 0; i < count; ++i) {
      const TextureResource *texture = textures.get(material.textures[i]);
      assert(texture != nullptr);
      imageInfos[i] = {texture->sampler, texture->image.view(),
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = material.sets[frameIndex];
      writes[i].dstBinding = shader.layout.textures[i].binding;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(context->device(),
                           static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
  }
  material.dirtyFrames &= ~bit;
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

  // drawList is sorted by (pipeline, material) to minimise state changes.
  VkPipeline boundPipeline = VK_NULL_HANDLE;
  const MaterialResource *boundMaterial = nullptr;
  for (const DrawItem &draw : drawList) {
    if (draw.pipeline != boundPipeline) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              draw.pipelineLayout, 0, 1, &frame.descriptorSet, 0,
                              nullptr);
      boundPipeline = draw.pipeline;
      boundMaterial = nullptr;
    }
    if (draw.material != boundMaterial) {
      if (draw.material->sets[frameIndex] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                draw.pipelineLayout, 1, 1,
                                &draw.material->sets[frameIndex], 0, nullptr);
      }
      boundMaterial = draw.material;
    }

    const VkBuffer vertexBuffer = draw.mesh->vertexBuffer.handle();
    const VkDeviceSize offset = 0;
    vkCmdPushConstants(cmd, draw.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushConstants), &draw.push);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, draw.mesh->indexBuffer.handle(), 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, draw.mesh->indexCount, 1, 0, 0, draw.firstJoint);
  }

  if (uiFrameReady) {
    imguiLayer->record(cmd);
  }

  vkCmdEndRendering(cmd);

  if (screenshotBuffer.handle() != VK_NULL_HANDLE) {
    transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshotBuffer.handle(), 1, &copy);
    // Make the copy visible to the host read after the fence wait.
    VkMemoryBarrier2 hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo hostDependency{};
    hostDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    hostDependency.memoryBarrierCount = 1;
    hostDependency.pMemoryBarriers = &hostBarrier;
    vkCmdPipelineBarrier2(cmd, &hostDependency);
    transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
  } else {
    transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_NONE, 0);
  }

  vkEndCommandBuffer(cmd);
}

// Waits for the frame just submitted, then writes its pixels to the requested
// PNG. Called with the capture buffer filled by that frame.
std::expected<void, Error> Renderer::Impl::saveScreenshot() {
  // The frame that was submitted last used the slot before frameIndex advances.
  const FrameData &frame = frames[frameIndex];
  if (auto r = vkCheck(vkWaitForFences(context->device(), 1, &frame.inFlight,
                                       VK_TRUE, UINT64_MAX),
                       kCode, "vkWaitForFences (screenshot)");
      !r) {
    return r;
  }
  screenshotBuffer.invalidate();

  const VkExtent2D extent = swapchain->extent();
  Image image;
  image.width = extent.width;
  image.height = extent.height;
  const std::size_t byteCount = static_cast<std::size_t>(extent.width) * extent.height * 4;
  image.pixels.resize(byteCount);
  std::memcpy(image.pixels.data(), screenshotBuffer.mapped(), byteCount);

  const VkFormat format = swapchain->format();
  if (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM) {
    for (std::size_t i = 0; i < byteCount; i += 4) {
      std::swap(image.pixels[i], image.pixels[i + 2]);
    }
  }
  for (std::size_t i = 3; i < byteCount; i += 4) {
    image.pixels[i] = 255;
  }

  const std::filesystem::path path = *screenshotPath;
  screenshotBuffer = GpuBuffer();
  screenshotPath.reset();
  return savePng(path, image);
}

std::expected<void, Error> Renderer::Impl::render(const Scene &scene,
                                                   const Camera &camera) {
  PAPER_ZONE("Renderer::render");
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
  // This frame's fence is signalled, so every frame up to frameNumber - 2 has
  // finished: resources retired for them can go.
  collectGarbage();

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
  uniforms.ambient = glm::vec4(scene.ambientLight, 0.0F);

  // Lights: a list so other kinds (point, spot) can be added later.
  std::size_t lightCount = 0;
  for (const auto [entity, world, light] :
       scene.registry().view<const WorldTransform, const DirectionalLight>().each()) {
    (void)entity;
    if (lightCount == kMaxLights) {
      if (!warnedAboutLightLimit) {
        spdlog::warn("More than {} lights in the scene; ignoring the rest",
                     kMaxLights);
        warnedAboutLightLimit = true;
      }
      break;
    }
    const glm::vec3 direction =
        glm::normalize(glm::mat3(world.matrix) * glm::vec3(0.0F, 0.0F, -1.0F));
    uniforms.lights[lightCount++] = {glm::vec4(direction, 0.0F),
                                     glm::vec4(light.color, light.intensity)};
  }
  uniforms.lightCount = glm::ivec4(static_cast<int>(lightCount), 0, 0, 0);
  std::memcpy(frame.uniforms.mapped(), &uniforms, sizeof(uniforms));
  frame.uniforms.flush();

  // Built before acquiring an image so a failure (e.g. pipeline creation)
  // leaves the swapchain untouched.
  drawList.clear();
  std::uint32_t jointCount = 0;
  auto *jointPalette = static_cast<glm::mat4 *>(frame.joints.mapped());
  for (const auto [entity, world, meshRenderer] :
       scene.registry()
           .view<const WorldTransform, const MeshRenderer>()
           .each()) {
    const MeshResource *mesh = meshes.get(meshRenderer.mesh);
    MaterialResource *material = materials.get(meshRenderer.material);
    assert(mesh != nullptr && "MeshRenderer references a stale mesh handle");
    assert(material != nullptr &&
           "MeshRenderer references a stale material handle");
    if (mesh == nullptr || material == nullptr) {
      continue;
    }
    const ShaderResource *shader = shaders.get(material->shader);
    assert(shader != nullptr);
    auto pipeline = getPipeline(material->shader, *shader, material->state);
    if (!pipeline) {
      return std::unexpected(pipeline.error());
    }
    updateMaterial(*material, *shader);
    std::uint32_t firstJoint = 0;
    if (const auto *pose = scene.registry().try_get<SkinPose>(entity)) {
      if (jointCount + pose->joints.size() > kMaxJointMatrices) {
        spdlog::warn("Too many skinned joints this frame ({}); skipping a mesh", kMaxJointMatrices);
        continue;
      }
      firstJoint = jointCount;
      std::memcpy(jointPalette + jointCount, pose->joints.data(),
                  pose->joints.size() * sizeof(glm::mat4));
      jointCount += static_cast<std::uint32_t>(pose->joints.size());
    }
    drawList.push_back({mesh,
                        material,
                        *pipeline,
                        shader->pipelineLayout,
                        {world.matrix, glm::transpose(glm::inverse(world.matrix))},
                        firstJoint});
  }
  if (jointCount > 0) {
    frame.joints.flush();
  }
  {
    PAPER_ZONE("sort draw list");
    std::ranges::sort(drawList, [](const DrawItem &a, const DrawItem &b) {
      return std::pair(sortKey(a.pipeline), sortKey(a.material)) <
             std::pair(sortKey(b.pipeline), sortKey(b.material));
    });
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

  if (screenshotPath) {
    const VkExtent2D captureExtent = swapchain->extent();
    auto buffer = GpuBuffer::create(
        *context, static_cast<VkDeviceSize>(captureExtent.width) * captureExtent.height * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    if (!buffer) {
      return std::unexpected(buffer.error());
    }
    screenshotBuffer = std::move(*buffer);
  }

  uiFrameReady = uiCallback != nullptr;
  if (uiFrameReady) {
    PAPER_ZONE("build UI");
    imguiLayer->beginFrame();
    uiCallback();
    imguiLayer->endFrame();
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
  ++frameNumber;

  // Written before presenting; a failure is reported after the frame is out.
  std::expected<void, Error> screenshot;
  if (screenshotBuffer.handle() != VK_NULL_HANDLE) {
    screenshot = saveScreenshot();
  }

  const VkSwapchainKHR swapchainHandle = swapchain->handle();
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinished[imageIndex];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchainHandle;
  presentInfo.pImageIndices = &imageIndex;
  PAPER_ZONE("present");
  const VkResult presented =
      vkQueuePresentKHR(context->presentQueue(), &presentInfo);
  frameIndex = (frameIndex + 1) % kFramesInFlight;

  if (!screenshot) {
    return screenshot;
  }
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

std::expected<ShaderHandle, Error>
Renderer::createShader(const ShaderSource &source) {
  return m_pImpl->createShader(source);
}

void Renderer::destroyShader(ShaderHandle shader) {
  m_pImpl->destroyShader(shader);
}

std::expected<MaterialHandle, Error>
Renderer::createMaterial(ShaderHandle shader, const RenderState &state) {
  return m_pImpl->createMaterial(shader, state);
}

void Renderer::destroyMaterial(MaterialHandle material) {
  m_pImpl->destroyMaterial(material);
}

std::expected<TextureHandle, Error>
Renderer::createTexture(const Image &image, ColorSpace colorSpace,
                        const SamplerSettings &sampler) {
  return m_pImpl->createTexture(image, colorSpace, sampler);
}

std::expected<TextureHandle, Error>
Renderer::loadTexture(const std::filesystem::path &path, ColorSpace colorSpace,
                      const SamplerSettings &sampler) {
  auto image = loadImage(path);
  if (!image) {
    return std::unexpected(image.error());
  }
  return m_pImpl->createTexture(*image, colorSpace, sampler);
}

TextureHandle Renderer::loadTextureOrDefault(
    const std::filesystem::path &path, ColorSpace colorSpace,
    DefaultTexture fallback, const SamplerSettings &sampler) {
  auto texture = loadTexture(path, colorSpace, sampler);
  if (!texture) {
    spdlog::warn("Using the default texture instead of '{}': {}", path.string(),
                 texture.error().message);
    return defaultTexture(fallback);
  }
  return *texture;
}

TextureHandle Renderer::defaultTexture(DefaultTexture which) const {
  return m_pImpl->defaultTextures[static_cast<std::size_t>(which)];
}

void Renderer::destroyTexture(TextureHandle texture) {
  m_pImpl->destroyTexture(texture);
}

std::expected<void, Error> Renderer::setMaterialTexture(
    MaterialHandle material, std::string_view slot, TextureHandle texture) {
  return m_pImpl->setMaterialTexture(material, slot, texture);
}

std::expected<void, Error>
Renderer::setMaterialParam(MaterialHandle material, std::string_view name,
                           const MaterialParam &value) {
  return m_pImpl->setMaterialParam(material, name, value);
}

bool Renderer::hasMaterialParam(ShaderHandle shader,
                                std::string_view name) const {
  const ShaderResource *resource = m_pImpl->shaders.get(shader);
  return resource != nullptr && resource->layout.find(name) != nullptr;
}

bool Renderer::hasTextureSlot(ShaderHandle shader, std::string_view name) const {
  const ShaderResource *resource = m_pImpl->shaders.get(shader);
  return resource != nullptr && resource->layout.findTexture(name) >= 0;
}

std::expected<ModelHandle, Error>
Renderer::loadModel(const std::filesystem::path &path, ShaderHandle shader) {
  if (m_pImpl->shaders.get(shader) == nullptr) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "loadModel: invalid shader handle"});
  }
  auto model = importGltf(*this, path, shader);
  if (!model) {
    return std::unexpected(model.error());
  }
  return m_pImpl->models.create(std::move(*model));
}

const Model &Renderer::model(ModelHandle model) const {
  const Model *resource = m_pImpl->models.get(model);
  assert(resource != nullptr && "model: stale or invalid handle");
  return *resource;
}

void Renderer::destroyModel(ModelHandle model) {
  auto taken = m_pImpl->models.destroy(model);
  assert(taken.has_value() && "destroyModel: stale or invalid handle");
  if (taken) {
    destroyModelResources(*this, *taken);
  }
}

void Renderer::setUiCallback(std::function<void()> callback) {
  m_pImpl->uiCallback = std::move(callback);
}

bool Renderer::uiWantsMouse() const {
  return m_pImpl->uiCallback && m_pImpl->imguiLayer->wantsMouse();
}

bool Renderer::uiWantsKeyboard() const {
  return m_pImpl->uiCallback && m_pImpl->imguiLayer->wantsKeyboard();
}

void Renderer::requestScreenshot(const std::filesystem::path &path) {
  m_pImpl->screenshotPath = path;
}

std::expected<void, Error> Renderer::render(const Scene &scene,
                                            const Camera &camera) {
  return m_pImpl->render(scene, camera);
}
} // namespace Paper
