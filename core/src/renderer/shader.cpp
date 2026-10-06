#include "core/renderer/shader.h"

#include "core/vulkan_context.h"

#include "vk_check.h"

#include <fstream>
#include <string>

namespace Paper {

std::expected<std::unique_ptr<Shader>, Error>
Shader::load(const VulkanContext &context,
             const std::filesystem::path &spirvPath, ShaderStage stage) {
  std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
  if (!file) {
    return std::unexpected(Error{
        ErrorCode::Io, "Cannot open shader file: " + spirvPath.string()});
  }
  const std::streamsize size = file.tellg();
  if (size <= 0 || size % sizeof(std::uint32_t) != 0) {
    return std::unexpected(Error{
        ErrorCode::Shader, "Not a valid SPIR-V file: " + spirvPath.string()});
  }

  std::unique_ptr<Shader> shader(new Shader(context, stage));
  shader->m_spirv.resize(static_cast<std::size_t>(size) /
                         sizeof(std::uint32_t));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(shader->m_spirv.data()), size)) {
    return std::unexpected(Error{
        ErrorCode::Io, "Failed to read shader file: " + spirvPath.string()});
  }

  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = static_cast<std::size_t>(size);
  info.pCode = shader->m_spirv.data();
  if (auto r = vkCheck(vkCreateShaderModule(context.device(), &info, nullptr,
                                            &shader->m_module),
                       ErrorCode::Shader, "vkCreateShaderModule");
      !r) {
    return std::unexpected(r.error());
  }
  return shader;
}

Shader::~Shader() {
  if (m_module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(m_context.device(), m_module, nullptr);
  }
}
} // namespace Paper
