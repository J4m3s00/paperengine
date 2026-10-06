#pragma once

#include "core/handle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Paper {
// Fixed vertex layout shared by every shader (see ADR 0004):
// location 0 position, 1 normal, 2 uv, 3 tangent (xyz + handedness in w).
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 tangent;
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
