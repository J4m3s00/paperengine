#pragma once

#include "core/error.h"
#include "core/handle.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <vector>

namespace Paper {
class VulkanContext;

enum class ShaderStage {
  Vertex,
  Fragment,
};

// SPIR-V files of a graphics shader. Relative paths are resolved against
// `<assetRoot>/shaders`.
struct ShaderSource {
  std::filesystem::path vertex;
  std::filesystem::path fragment;
};

struct ShaderTag;
using ShaderHandle = Handle<ShaderTag>;

// A SPIR-V shader module loaded from a `.spv` file produced by glslc.
// Borrows the VulkanContext, which must outlive the Shader.
class Shader {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<Shader>, Error>
  load(const VulkanContext &context, const std::filesystem::path &spirvPath,
       ShaderStage stage);
  ~Shader();

  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;
  Shader(Shader &&) = delete;
  Shader &operator=(Shader &&) = delete;

  [[nodiscard]] VkShaderModule module() const { return m_module; }
  [[nodiscard]] ShaderStage stage() const { return m_stage; }
  [[nodiscard]] const std::vector<std::uint32_t> &spirv() const {
    return m_spirv;
  }

private:
  Shader(const VulkanContext &context, ShaderStage stage)
      : m_context(context), m_stage(stage) {}

  const VulkanContext &m_context;
  ShaderStage m_stage;
  std::vector<std::uint32_t> m_spirv;
  VkShaderModule m_module = VK_NULL_HANDLE;
};
} // namespace Paper
