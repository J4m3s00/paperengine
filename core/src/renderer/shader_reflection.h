#pragma once

#include "base/error.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Paper {
enum class ParamType { Float, Int, Vec2, Vec3, Vec4, Mat4, Unsupported };

[[nodiscard]] std::uint32_t paramSize(ParamType type);
[[nodiscard]] const char *paramTypeName(ParamType type);

struct ParamInfo {
  std::string name;
  ParamType type;
  std::uint32_t offset;
};

// A combined image sampler (2D) in set 1, binding >= 1.
struct TextureSlotInfo {
  std::string name;
  std::uint32_t binding;
};

// Set 1 of a shader (ADR 0004): the uniform block at binding 0 and the texture
// slots at the following bindings. blockSize == 0 means the shader has no
// material parameters.
struct MaterialLayout {
  std::uint32_t blockSize = 0;
  std::vector<ParamInfo> params;
  std::vector<TextureSlotInfo> textures; // Sorted by binding.
  // The vertex shader reads joint indices/weights (locations 4 and 5).
  bool skinned = false;

  [[nodiscard]] const ParamInfo *find(std::string_view name) const;
  // Index into `textures`, or -1.
  [[nodiscard]] int findTexture(std::string_view name) const;
};

// Reflects both stages, validates them against the engine conventions
// (fixed vertex inputs, set 0 reserved, set 1 = uniform block at binding 0 plus
// 2D combined image samplers) and returns the material layout.
[[nodiscard]] std::expected<MaterialLayout, Error>
reflectMaterialLayout(std::span<const std::uint32_t> vertexSpirv,
                      std::span<const std::uint32_t> fragmentSpirv);
} // namespace Paper
