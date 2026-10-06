#pragma once

#include "core/handle.h"

#include <glm/glm.hpp>

#include <variant>

namespace Paper {
enum class CullMode {
  None,
  Back,
  Front,
};

// OPAQUE and MASK (alpha test) only; BLEND is not supported yet. Shaders see
// MASK through the specialization constant `layout(constant_id = 0) const bool
// ALPHA_MASK` and are expected to `discard` themselves.
enum class AlphaMode {
  Opaque,
  Mask,
};

struct RenderState {
  CullMode cullMode = CullMode::Back;
  AlphaMode alphaMode = AlphaMode::Opaque;
  bool depthTest = true;

  friend auto operator<=>(const RenderState &, const RenderState &) = default;
};

// A value for a named parameter in the shader's set 1 uniform block.
using MaterialParam =
    std::variant<float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

struct MaterialTag;
using MaterialHandle = Handle<MaterialTag>;
} // namespace Paper
