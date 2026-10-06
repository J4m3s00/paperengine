#pragma once

#include "core/error.h"

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

// The set 1, binding 0 uniform block of a shader (ADR 0004). blockSize == 0
// means the shader has no material parameters.
struct MaterialLayout {
  std::uint32_t blockSize = 0;
  std::vector<ParamInfo> params;

  [[nodiscard]] const ParamInfo *find(std::string_view name) const;
};

// Reflects both stages, validates them against the engine conventions
// (fixed vertex inputs, set 0 reserved, only set 1 binding 0 uniform buffer
// for now) and returns the material parameter layout.
[[nodiscard]] std::expected<MaterialLayout, Error>
reflectMaterialLayout(std::span<const std::uint32_t> vertexSpirv,
                      std::span<const std::uint32_t> fragmentSpirv);
} // namespace Paper
