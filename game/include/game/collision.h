#pragma once

#include "base/error.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

// Collision for the simulation (ADR 0010). Units are Source units (1 unit =
// 1 inch) and the world is Y up, like the renderer. Everything here is
// deterministic and `trace*` / `pointSolid` never allocate.
namespace Paper {
// dot(normal, p) == dist. The solid side of a brush plane is behind it.
struct Plane {
  glm::vec3 normal;
  float dist;
};

// An axis-aligned box relative to an entity origin; need not be centered
// (player hulls stand on their origin: mins.y == 0).
struct Hull {
  glm::vec3 mins;
  glm::vec3 maxs;
};

// A convex solid given by outward-facing planes. Must be closed (bounded).
struct BrushDesc {
  std::vector<Plane> planes;
};

// Axis-aligned box brush from its extents.
[[nodiscard]] BrushDesc boxBrush(const glm::vec3 &mins, const glm::vec3 &maxs);

struct TraceResult {
  // How far along start->end the hull got, in [0, 1]. 1 means no hit.
  float fraction = 1.0F;
  glm::vec3 endPos{0.0F};
  // Surface normal at the hit (zero when nothing was hit or startSolid).
  glm::vec3 normal{0.0F};
  // The trace began inside some brush (it may still move out of it).
  bool startSolid = false;
  // The whole trace is inside solid. fraction is 0.
  bool allSolid = false;
  // Index (in creation order) of the brush that was hit, or -1.
  std::int32_t brush = -1;
};

class CollisionWorld {
public:
  // Builds the acceleration structure. Fails if a brush is not a bounded
  // convex solid (fewer than 4 vertices from its planes).
  [[nodiscard]] static std::expected<CollisionWorld, Error>
  create(std::span<const BrushDesc> brushes);

  // Sweeps `hull` (positioned at `start`) to `end` and reports the first hit.
  [[nodiscard]] TraceResult traceHull(const glm::vec3 &start, const glm::vec3 &end,
                                      const Hull &hull) const;
  // A zero-size hull.
  [[nodiscard]] TraceResult traceRay(const glm::vec3 &start, const glm::vec3 &end) const {
    return traceHull(start, end, Hull{glm::vec3(0.0F), glm::vec3(0.0F)});
  }
  // True if `p` is inside any brush.
  [[nodiscard]] bool pointSolid(const glm::vec3 &p) const;

  [[nodiscard]] std::size_t brushCount() const { return m_brushes.size(); }

private:
  struct Brush {
    std::uint32_t firstPlane;
    std::uint32_t planeCount;
    glm::vec3 mins;
    glm::vec3 maxs;
  };
  struct Node {
    glm::vec3 mins;
    glm::vec3 maxs;
    // Leaf: first/count index m_leafBrushes. Inner: first = left child
    // (right child is first + 1), count = 0.
    std::uint32_t first;
    std::uint32_t count;
  };

  CollisionWorld() = default;
  void buildBvh();
  void traceBrush(const Brush &brush, std::int32_t brushIndex, const glm::vec3 &start,
                  const glm::vec3 &end, const Hull &hull, TraceResult &result) const;

  // All brushes' planes (including generated bevels), contiguous per brush.
  std::vector<Plane> m_planes;
  std::vector<Brush> m_brushes;
  std::vector<Node> m_nodes;
  // Brush indices, grouped by BVH leaf.
  std::vector<std::uint32_t> m_leafBrushes;
};
} // namespace Paper
