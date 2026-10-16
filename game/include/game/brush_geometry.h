#pragma once

#include "game/collision.h"

#include <vector>

namespace Paper {
// One polygonal face of a convex brush. Vertices are counter-clockwise seen
// from outside (the side `plane.normal` points to).
struct BrushFace {
  Plane plane;
  std::vector<glm::vec3> vertices;
};

// The visible faces of `brush`: each plane clipped by all the others. Planes
// that contribute no area (redundant) are skipped. For rendering and tools;
// collision uses the planes directly.
[[nodiscard]] std::vector<BrushFace> brushFaces(const BrushDesc &brush);
} // namespace Paper
