#include "game/brush_geometry.h"

#include <cmath>

namespace Paper {
namespace {
constexpr float kHugeQuad = 20000.0F;
constexpr float kClipEpsilon = 0.01F;

// Keeps the part of `polygon` behind (or on) `plane`.
std::vector<glm::vec3> clip(const std::vector<glm::vec3> &polygon, const Plane &plane) {
  std::vector<glm::vec3> out;
  const std::size_t n = polygon.size();
  for (std::size_t i = 0; i < n; ++i) {
    const glm::vec3 &a = polygon[i];
    const glm::vec3 &b = polygon[(i + 1) % n];
    const float da = glm::dot(plane.normal, a) - plane.dist;
    const float db = glm::dot(plane.normal, b) - plane.dist;
    const bool aIn = da <= kClipEpsilon;
    const bool bIn = db <= kClipEpsilon;
    if (aIn) {
      out.push_back(a);
    }
    if (aIn != bIn) {
      const float t = da / (da - db);
      out.push_back(a + (b - a) * t);
    }
  }
  return out;
}

float polygonArea(const std::vector<glm::vec3> &polygon) {
  glm::vec3 sum(0.0F);
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    sum += glm::cross(polygon[i], polygon[(i + 1) % polygon.size()]);
  }
  return 0.5F * glm::length(sum);
}
} // namespace

std::vector<BrushFace> brushFaces(const BrushDesc &brush) {
  std::vector<BrushFace> faces;
  for (std::size_t i = 0; i < brush.planes.size(); ++i) {
    Plane plane = brush.planes[i];
    const float length = glm::length(plane.normal);
    if (length < 1e-6F) {
      continue;
    }
    plane.normal /= length;
    plane.dist /= length;

    // A huge quad on the plane, counter-clockwise seen from outside.
    const glm::vec3 helper = std::abs(plane.normal.y) < 0.9F ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(helper, plane.normal));
    const glm::vec3 v = glm::cross(plane.normal, u);
    const glm::vec3 center = plane.normal * plane.dist;
    std::vector<glm::vec3> polygon{center - u * kHugeQuad - v * kHugeQuad,
                                   center + u * kHugeQuad - v * kHugeQuad,
                                   center + u * kHugeQuad + v * kHugeQuad,
                                   center - u * kHugeQuad + v * kHugeQuad};
    for (std::size_t j = 0; j < brush.planes.size() && polygon.size() >= 3; ++j) {
      if (j == i) {
        continue;
      }
      Plane other = brush.planes[j];
      const float otherLength = glm::length(other.normal);
      other.normal /= otherLength;
      other.dist /= otherLength;
      // A duplicate of this plane would clip away everything: skip it.
      if (glm::dot(other.normal, plane.normal) > 0.9999F && std::abs(other.dist - plane.dist) < 1e-3F) {
        continue;
      }
      polygon = clip(polygon, other);
    }
    if (polygon.size() >= 3 && polygonArea(polygon) > 1e-2F) {
      faces.push_back({plane, std::move(polygon)});
    }
  }
  return faces;
}
} // namespace Paper
