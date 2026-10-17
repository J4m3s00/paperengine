#pragma once

#include "core/handle.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Paper {
// Fixed vertex layout shared by every shader (see ADR 0004):
// location 0 position, 1 normal, 2 uv, 3 tangent (xyz + handedness in w),
// 4 joint indices (4 x uint8) and 5 joint weights (4 x unorm8, sum 255) for
// skinning. Meshes that are not skinned leave 4 and 5 zero.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 tangent;
  std::array<std::uint8_t, 4> joints{};
  std::array<std::uint8_t, 4> weights{};
};

// CPU-side mesh: one vertex buffer + one index buffer = one draw.
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
};

struct MeshTag;
using MeshHandle = Handle<MeshTag>;

// Axis-aligned cube centred at the origin, counter-clockwise front faces.
[[nodiscard]] MeshData makeCube(float size = 1.0F);
} // namespace Paper
