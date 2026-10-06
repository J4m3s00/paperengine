#pragma once

#include "core/error.h"
#include "core/renderer/camera.h"
#include "core/renderer/mesh.h"
#include "core/vulkan_context.h"

#include <glm/glm.hpp>

#include <expected>
#include <filesystem>
#include <memory>

namespace Paper {
class Scene;
class Window;

struct RendererConfig {
  VulkanConfig vulkan;
  // Linear color; the swapchain is sRGB.
  glm::vec4 clearColor{0.02F, 0.02F, 0.05F, 1.0F};
  // Directory containing `shaders/*.spv`. Empty: `<executable dir>` if it has
  // a `shaders` folder, otherwise the build directory (dev builds).
  std::filesystem::path assetRoot;
};

// Owns the VulkanContext, the Swapchain and all per-frame resources.
// `window` must outlive the Renderer.
class Renderer {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<Renderer>, Error>
  create(const Window &window, const RendererConfig &config = {});
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) = delete;
  Renderer &operator=(Renderer &&) = delete;

  // Uploads a mesh to the GPU (synchronously) and returns a handle to it.
  [[nodiscard]] std::expected<MeshHandle, Error>
  createMesh(const MeshData &data);
  // Frees the mesh; the handle becomes invalid. Waits for the GPU to go idle
  // (a deferred deletion queue arrives with the material system).
  void destroyMesh(MeshHandle mesh);

  // Renders and presents one frame of `scene` seen through `camera`. Run
  // TransformSystem::update(scene) first. Handles resize and minimise
  // internally; while the window is minimised this is a no-op.
  [[nodiscard]] std::expected<void, Error> render(const Scene &scene,
                                                  const Camera &camera);

private:
  struct Impl;
  explicit Renderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> m_pImpl;
};
} // namespace Paper
