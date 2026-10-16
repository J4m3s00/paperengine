#include "game/collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace Paper {
namespace {
// Keeps a hull this far in front of a surface so float error cannot push it in.
constexpr float kSurfaceEpsilon = 0.125F;
constexpr float kVertexEpsilon = 0.01F;
constexpr std::size_t kMaxBvhDepth = 64;
constexpr std::uint32_t kLeafSize = 4;

struct BrushGeometry {
  std::vector<glm::vec3> vertices;
  glm::vec3 mins;
  glm::vec3 maxs;
};

// Corner points of the convex brush: every plane triple intersection that lies
// inside all planes.
BrushGeometry computeVertices(const std::vector<Plane> &planes) {
  BrushGeometry geo;
  const std::size_t n = planes.size();
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      for (std::size_t k = j + 1; k < n; ++k) {
        const glm::vec3 &n1 = planes[i].normal;
        const glm::vec3 &n2 = planes[j].normal;
        const glm::vec3 &n3 = planes[k].normal;
        const float denom = glm::dot(n1, glm::cross(n2, n3));
        if (std::abs(denom) < 1e-6F) {
          continue;
        }
        const glm::vec3 p = (planes[i].dist * glm::cross(n2, n3) +
                             planes[j].dist * glm::cross(n3, n1) +
                             planes[k].dist * glm::cross(n1, n2)) /
                            denom;
        const bool inside = std::ranges::all_of(planes, [&](const Plane &plane) {
          return glm::dot(plane.normal, p) - plane.dist <= kVertexEpsilon;
        });
        if (!inside) {
          continue;
        }
        const bool duplicate = std::ranges::any_of(geo.vertices, [&](const glm::vec3 &v) {
          return glm::distance(v, p) < kVertexEpsilon * 10.0F;
        });
        if (!duplicate) {
          geo.vertices.push_back(p);
        }
      }
    }
  }
  geo.mins = glm::vec3(INFINITY);
  geo.maxs = glm::vec3(-INFINITY);
  for (const glm::vec3 &v : geo.vertices) {
    geo.mins = glm::min(geo.mins, v);
    geo.maxs = glm::max(geo.maxs, v);
  }
  return geo;
}

bool samePlane(const Plane &a, const Plane &b) {
  return glm::distance(a.normal, b.normal) < 1e-4F && std::abs(a.dist - b.dist) < 1e-3F;
}

// Adds `candidate` if all brush vertices lie behind it (flipping it if they all
// lie in front) and it is not a duplicate. Candidate must pass through a vertex.
void addIfSupporting(std::vector<Plane> &planes, const std::vector<glm::vec3> &vertices,
                     glm::vec3 normal, const glm::vec3 &through) {
  const float length = glm::length(normal);
  if (length < 1e-4F) {
    return;
  }
  normal /= length;
  float dist = glm::dot(normal, through);
  float maxOver = -INFINITY;
  float minOver = INFINITY;
  for (const glm::vec3 &v : vertices) {
    const float over = glm::dot(normal, v) - dist;
    maxOver = std::max(maxOver, over);
    minOver = std::min(minOver, over);
  }
  if (maxOver > kVertexEpsilon) {
    if (minOver < -kVertexEpsilon) {
      return;
    }
    normal = -normal;
    dist = -dist;
  }
  const Plane plane{normal, dist};
  if (std::ranges::none_of(planes, [&](const Plane &p) { return samePlane(p, plane); })) {
    planes.push_back(plane);
  }
}

// Planes needed so that expanding by an axis-aligned hull is exact: the
// brush's AABB faces, plus for each brush edge and box axis the plane that
// contains both (Quake 3 style bevels).
std::vector<Plane> withBevels(std::vector<Plane> planes, const BrushGeometry &geo) {
  const std::size_t original = planes.size();
  const glm::vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (const glm::vec3 &axis : axes) {
    addIfSupporting(planes, geo.vertices, axis, geo.maxs);
    addIfSupporting(planes, geo.vertices, -axis, geo.mins);
  }
  for (std::size_t i = 0; i < original; ++i) {
    for (std::size_t j = i + 1; j < original; ++j) {
      // Edge = the vertices shared by both planes.
      glm::vec3 a(0.0F);
      glm::vec3 b(0.0F);
      float longest = 0.0F;
      std::vector<glm::vec3> shared;
      for (const glm::vec3 &v : geo.vertices) {
        if (std::abs(glm::dot(planes[i].normal, v) - planes[i].dist) < kVertexEpsilon * 10.0F &&
            std::abs(glm::dot(planes[j].normal, v) - planes[j].dist) < kVertexEpsilon * 10.0F) {
          shared.push_back(v);
        }
      }
      if (shared.size() < 2) {
        continue;
      }
      for (std::size_t p = 0; p < shared.size(); ++p) {
        for (std::size_t q = p + 1; q < shared.size(); ++q) {
          const float d = glm::distance(shared[p], shared[q]);
          if (d > longest) {
            longest = d;
            a = shared[p];
            b = shared[q];
          }
        }
      }
      const glm::vec3 edge = b - a;
      for (const glm::vec3 &axis : axes) {
        addIfSupporting(planes, geo.vertices, glm::cross(edge, axis), a);
      }
    }
  }
  return planes;
}

bool overlaps(const glm::vec3 &minsA, const glm::vec3 &maxsA, const glm::vec3 &minsB,
              const glm::vec3 &maxsB) {
  return minsA.x <= maxsB.x && maxsA.x >= minsB.x && minsA.y <= maxsB.y &&
         maxsA.y >= minsB.y && minsA.z <= maxsB.z && maxsA.z >= minsB.z;
}
} // namespace

BrushDesc boxBrush(const glm::vec3 &mins, const glm::vec3 &maxs) {
  return BrushDesc{{{{1, 0, 0}, maxs.x},
                    {{-1, 0, 0}, -mins.x},
                    {{0, 1, 0}, maxs.y},
                    {{0, -1, 0}, -mins.y},
                    {{0, 0, 1}, maxs.z},
                    {{0, 0, -1}, -mins.z}}};
}

std::expected<CollisionWorld, Error> CollisionWorld::create(std::span<const BrushDesc> brushes) {
  CollisionWorld world;
  world.m_brushes.reserve(brushes.size());
  for (std::size_t i = 0; i < brushes.size(); ++i) {
    std::vector<Plane> planes = brushes[i].planes;
    for (Plane &plane : planes) {
      const float length = glm::length(plane.normal);
      if (length < 1e-6F) {
        return std::unexpected(Error{ErrorCode::InvalidArgument,
                                     "brush " + std::to_string(i) + " has a zero-length normal"});
      }
      plane.normal /= length;
      plane.dist /= length;
    }
    const BrushGeometry geo = computeVertices(planes);
    if (geo.vertices.size() < 4) {
      return std::unexpected(
          Error{ErrorCode::InvalidArgument,
                "brush " + std::to_string(i) + " is not a bounded convex solid"});
    }
    const std::vector<Plane> all = withBevels(std::move(planes), geo);
    world.m_brushes.push_back({static_cast<std::uint32_t>(world.m_planes.size()),
                               static_cast<std::uint32_t>(all.size()), geo.mins, geo.maxs});
    world.m_planes.insert(world.m_planes.end(), all.begin(), all.end());
  }
  world.buildBvh();
  return world;
}

void CollisionWorld::buildBvh() {
  std::vector<std::uint32_t> order(m_brushes.size());
  std::iota(order.begin(), order.end(), 0U);
  m_nodes.clear();
  m_nodes.push_back({});

  struct Builder {
    CollisionWorld &world;
    std::vector<std::uint32_t> &order;

    void build(std::uint32_t node, std::uint32_t begin, std::uint32_t end) {
      glm::vec3 mins(INFINITY);
      glm::vec3 maxs(-INFINITY);
      glm::vec3 cmins(INFINITY);
      glm::vec3 cmaxs(-INFINITY);
      for (std::uint32_t i = begin; i < end; ++i) {
        const Brush &b = world.m_brushes[order[i]];
        mins = glm::min(mins, b.mins);
        maxs = glm::max(maxs, b.maxs);
        const glm::vec3 c = (b.mins + b.maxs) * 0.5F;
        cmins = glm::min(cmins, c);
        cmaxs = glm::max(cmaxs, c);
      }
      world.m_nodes[node].mins = mins;
      world.m_nodes[node].maxs = maxs;
      if (end - begin <= kLeafSize) {
        world.m_nodes[node].first = begin;
        world.m_nodes[node].count = end - begin;
        return;
      }
      const glm::vec3 extent = cmaxs - cmins;
      const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
      const std::uint32_t mid = begin + (end - begin) / 2;
      std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
                       [&](std::uint32_t a, std::uint32_t b) {
                         return world.m_brushes[a].mins[axis] + world.m_brushes[a].maxs[axis] <
                                world.m_brushes[b].mins[axis] + world.m_brushes[b].maxs[axis];
                       });
      const auto left = static_cast<std::uint32_t>(world.m_nodes.size());
      world.m_nodes.push_back({});
      world.m_nodes.push_back({});
      world.m_nodes[node].first = left;
      world.m_nodes[node].count = 0;
      build(left, begin, mid);
      build(left + 1, mid, end);
    }
  };
  Builder{*this, order}.build(0, 0, static_cast<std::uint32_t>(order.size()));
  m_leafBrushes = std::move(order);
}

void CollisionWorld::traceBrush(const Brush &brush, std::int32_t brushIndex,
                                const glm::vec3 &start, const glm::vec3 &end, const Hull &hull,
                                TraceResult &result) const {
  float enter = -1.0F;
  float leave = 1.0F;
  bool startOut = false;
  bool getOut = false;
  const Plane *clip = nullptr;

  for (std::uint32_t i = 0; i < brush.planeCount; ++i) {
    const Plane &plane = m_planes[brush.firstPlane + i];
    // The hull corner that touches this plane first.
    const glm::vec3 offset(plane.normal.x < 0.0F ? hull.maxs.x : hull.mins.x,
                           plane.normal.y < 0.0F ? hull.maxs.y : hull.mins.y,
                           plane.normal.z < 0.0F ? hull.maxs.z : hull.mins.z);
    const float dist = plane.dist - glm::dot(offset, plane.normal);
    const float d1 = glm::dot(start, plane.normal) - dist;
    const float d2 = glm::dot(end, plane.normal) - dist;

    if (d2 > 0.0F) {
      getOut = true;
    }
    if (d1 > 0.0F) {
      startOut = true;
    }
    // Entirely in front of this plane: cannot touch the brush.
    if (d1 > 0.0F && (d2 >= kSurfaceEpsilon || d2 >= d1)) {
      return;
    }
    if (d1 <= 0.0F && d2 <= 0.0F) {
      continue;
    }
    if (d1 > d2) {
      const float f = std::max((d1 - kSurfaceEpsilon) / (d1 - d2), 0.0F);
      if (f > enter) {
        enter = f;
        clip = &plane;
      }
    } else {
      const float f = (d1 + kSurfaceEpsilon) / (d1 - d2);
      leave = std::min(leave, f);
    }
  }

  if (!startOut) {
    result.startSolid = true;
    if (!getOut) {
      result.allSolid = true;
    }
    return;
  }
  if (enter < leave && enter > -1.0F && enter < result.fraction) {
    result.fraction = std::max(enter, 0.0F);
    result.normal = clip->normal;
    result.brush = brushIndex;
  }
}

TraceResult CollisionWorld::traceHull(const glm::vec3 &start, const glm::vec3 &end,
                                      const Hull &hull) const {
  TraceResult result;
  if (!m_nodes.empty() && !m_brushes.empty()) {
    // Everything the hull can touch while moving from start to end.
    const glm::vec3 queryMins = glm::min(start, end) + hull.mins - glm::vec3(kSurfaceEpsilon);
    const glm::vec3 queryMaxs = glm::max(start, end) + hull.maxs + glm::vec3(kSurfaceEpsilon);

    std::array<std::uint32_t, kMaxBvhDepth> stack;
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
      const Node &node = m_nodes[stack[--top]];
      if (!overlaps(node.mins, node.maxs, queryMins, queryMaxs)) {
        continue;
      }
      if (node.count == 0) {
        stack[top++] = node.first;
        stack[top++] = node.first + 1;
        continue;
      }
      for (std::uint32_t i = 0; i < node.count; ++i) {
        const std::uint32_t index = m_leafBrushes[node.first + i];
        const Brush &brush = m_brushes[index];
        if (overlaps(brush.mins, brush.maxs, queryMins, queryMaxs)) {
          traceBrush(brush, static_cast<std::int32_t>(index), start, end, hull, result);
        }
      }
    }
  }
  if (result.allSolid) {
    result.fraction = 0.0F;
  }
  result.endPos = start + (end - start) * result.fraction;
  return result;
}

bool CollisionWorld::pointSolid(const glm::vec3 &p) const {
  if (m_nodes.empty() || m_brushes.empty()) {
    return false;
  }
  std::array<std::uint32_t, kMaxBvhDepth> stack;
  std::size_t top = 0;
  stack[top++] = 0;
  while (top > 0) {
    const Node &node = m_nodes[stack[--top]];
    if (!overlaps(node.mins, node.maxs, p, p)) {
      continue;
    }
    if (node.count == 0) {
      stack[top++] = node.first;
      stack[top++] = node.first + 1;
      continue;
    }
    for (std::uint32_t i = 0; i < node.count; ++i) {
      const Brush &brush = m_brushes[m_leafBrushes[node.first + i]];
      if (!overlaps(brush.mins, brush.maxs, p, p)) {
        continue;
      }
      const bool inside = std::ranges::all_of(
          std::span(m_planes).subspan(brush.firstPlane, brush.planeCount),
          [&](const Plane &plane) { return glm::dot(plane.normal, p) - plane.dist <= 0.0F; });
      if (inside) {
        return true;
      }
    }
  }
  return false;
}
} // namespace Paper
