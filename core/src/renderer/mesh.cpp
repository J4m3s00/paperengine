#include "core/renderer/mesh.h"

#include <array>

namespace Paper {

MeshData makeCube(float size) {
  struct Face {
    glm::vec3 normal;
    glm::vec3 tangent;
  };
  // tangent x bitangent == normal (right-handed), so the corner order below is
  // counter-clockwise seen from outside.
  constexpr std::array<Face, 6> faces{{
      {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
      {{-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
      {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
      {{0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
      {{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}},
      {{0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F}},
  }};
  constexpr std::array<glm::vec2, 4> corners{
      {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};

  const float half = size * 0.5F;
  MeshData mesh;
  mesh.vertices.reserve(24);
  mesh.indices.reserve(36);
  for (const Face &face : faces) {
    const glm::vec3 bitangent = glm::cross(face.normal, face.tangent);
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const glm::vec2 &corner : corners) {
      mesh.vertices.push_back(
          {(face.normal + face.tangent * corner.x + bitangent * corner.y) * half,
           face.normal,
           corner * 0.5F + 0.5F,
           glm::vec4(face.tangent, 1.0F)});
    }
    for (const std::uint32_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
      mesh.indices.push_back(base + i);
    }
  }
  return mesh;
}
} // namespace Paper
