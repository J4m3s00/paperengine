#pragma once

#include "core/assets/image.h"
#include "core/assets/model.h"
#include "core/error.h"
#include "core/renderer/camera.h"
#include "core/renderer/material.h"
#include "core/renderer/mesh.h"
#include "core/renderer/shader.h"
#include "core/renderer/texture.h"
#include "core/vulkan_context.h"

#include <glm/glm.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>

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
  // Frees the mesh; the handle becomes invalid. The GPU memory is released once
  // the frames in flight that may still use it have finished.
  void destroyMesh(MeshHandle mesh);

  // Loads and reflects a graphics shader. Fails if the SPIR-V does not follow
  // the engine conventions (fixed vertex inputs, set 0 reserved, material
  // parameters in set 1 binding 0).
  [[nodiscard]] std::expected<ShaderHandle, Error>
  createShader(const ShaderSource &source);
  // The shader must not be used by any live material.
  void destroyShader(ShaderHandle shader);

  // Uploads an RGBA8 image as a sampled texture with a full mip chain.
  [[nodiscard]] std::expected<TextureHandle, Error>
  createTexture(const Image &image, ColorSpace colorSpace,
                const SamplerSettings &sampler = {});
  // Decodes the image file at `path` (relative paths are relative to the
  // working directory) and uploads it.
  [[nodiscard]] std::expected<TextureHandle, Error>
  loadTexture(const std::filesystem::path &path, ColorSpace colorSpace,
              const SamplerSettings &sampler = {});
  // Like loadTexture, but on failure logs a warning and returns the default
  // texture instead.
  [[nodiscard]] TextureHandle
  loadTextureOrDefault(const std::filesystem::path &path, ColorSpace colorSpace,
                       DefaultTexture fallback,
                       const SamplerSettings &sampler = {});
  [[nodiscard]] TextureHandle defaultTexture(DefaultTexture which) const;
  // The texture must not be assigned to any live material; default textures
  // cannot be destroyed.
  void destroyTexture(TextureHandle texture);

  // Whether `shader` declares a material parameter / texture slot of that name.
  [[nodiscard]] bool hasMaterialParam(ShaderHandle shader,
                                      std::string_view name) const;
  [[nodiscard]] bool hasTextureSlot(ShaderHandle shader,
                                    std::string_view name) const;

  // A material is a shader plus parameter values and render state.
  [[nodiscard]] std::expected<MaterialHandle, Error>
  createMaterial(ShaderHandle shader, const RenderState &state = {});
  void destroyMaterial(MaterialHandle material);
  // Sets a parameter of the shader's material block by name. Fails (and
  // changes nothing) for unknown names or a value of the wrong type.
  [[nodiscard]] std::expected<void, Error>
  setMaterialParam(MaterialHandle material, std::string_view name,
                   const MaterialParam &value);
  // Assigns a texture to a named sampler2D slot of the shader. Unassigned slots
  // use a default texture (see DefaultTexture).
  [[nodiscard]] std::expected<void, Error>
  setMaterialTexture(MaterialHandle material, std::string_view slot,
                     TextureHandle texture);

  // Loads a glTF/GLB file: meshes (one per primitive), textures and materials
  // are created on the GPU, and the node tree is returned as a Model. Imported
  // materials use `shader`; glTF properties are mapped onto the parameters and
  // texture slots it declares, by their glTF names:
  //   baseColorFactor (vec4), metallicFactor, roughnessFactor, emissiveFactor
  //   (vec3), normalScale, occlusionStrength, alphaCutoff;
  //   baseColorTexture, metallicRoughnessTexture, normalTexture,
  //   occlusionTexture, emissiveTexture.
  // Names the shader does not declare are skipped. See ADR 0007.
  [[nodiscard]] std::expected<ModelHandle, Error>
  loadModel(const std::filesystem::path &path, ShaderHandle shader);
  // The reference stays valid until the model is destroyed.
  [[nodiscard]] const Model &model(ModelHandle model) const;
  // Frees the model's meshes, materials and textures. Entities created with
  // Scene::instantiate must no longer be rendered.
  void destroyModel(ModelHandle model);

  // Saves the next rendered frame as a PNG (sRGB, as presented). The frame
  // is captured by the following render() call, which waits for the GPU and
  // returns an error if the file cannot be written.
  void requestScreenshot(const std::filesystem::path &path);

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
