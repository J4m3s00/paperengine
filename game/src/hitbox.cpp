#include "game/hitbox.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Paper {
float hitGroupMultiplier(HitGroup group) {
  switch (group) {
  case HitGroup::Head: return 4.0F;
  case HitGroup::Chest: return 1.0F;
  case HitGroup::Stomach: return 1.25F;
  case HitGroup::LeftArm:
  case HitGroup::RightArm: return 1.0F;
  case HitGroup::LeftLeg:
  case HitGroup::RightLeg: return 0.75F;
  }
  return 1.0F;
}

void FixedHitboxProvider::hitboxes(const PlayerPose &pose, HitboxSet &out) const {
  // Standing layout, feet at y = 0 (units). x is the player's right.
  struct Def {
    HitGroup group;
    glm::vec3 mins;
    glm::vec3 maxs;
  };
  static constexpr Def kStanding[] = {
      {HitGroup::Head, {-4.5F, 61.0F, -4.5F}, {4.5F, 72.0F, 4.5F}},
      {HitGroup::Chest, {-9.0F, 44.0F, -6.0F}, {9.0F, 61.0F, 6.0F}},
      {HitGroup::Stomach, {-8.0F, 30.0F, -5.5F}, {8.0F, 44.0F, 5.5F}},
      {HitGroup::LeftArm, {-16.0F, 40.0F, -4.0F}, {-9.0F, 61.0F, 4.0F}},
      {HitGroup::RightArm, {9.0F, 40.0F, -4.0F}, {16.0F, 61.0F, 4.0F}},
      {HitGroup::LeftLeg, {-8.5F, 0.0F, -5.0F}, {-0.5F, 30.0F, 5.0F}},
      {HitGroup::RightLeg, {0.5F, 0.0F, -5.0F}, {8.5F, 30.0F, 5.0F}},
  };
  // Crouching squashes the body to the 54 unit hull.
  const float scale = pose.ducked ? 54.0F / 72.0F : 1.0F;
  out.origin = pose.origin;
  out.yaw = pose.yaw;
  out.count = 0;
  for (const Def &d : kStanding) {
    out.boxes[out.count++] = {d.group, {d.mins.x, d.mins.y * scale, d.mins.z},
                              {d.maxs.x, d.maxs.y * scale, d.maxs.z}};
  }
}

std::optional<HitboxHit> rayVsHitboxes(const HitboxSet &set, const glm::vec3 &start,
                                       const glm::vec3 &dir, float maxDistance) {
  // Into the player's local frame (inverse of a rotation by `yaw` about Y).
  const float c = std::cos(set.yaw);
  const float s = std::sin(set.yaw);
  const auto toLocal = [&](const glm::vec3 &v) {
    return glm::vec3(c * v.x - s * v.z, v.y, s * v.x + c * v.z);
  };
  const glm::vec3 o = toLocal(start - set.origin);
  const glm::vec3 d = toLocal(dir);

  std::optional<HitboxHit> best;
  for (std::size_t i = 0; i < set.count; ++i) {
    const HitboxLocal &box = set.boxes[i];
    float tMin = 0.0F;
    float tMax = maxDistance;
    bool hit = true;
    for (int axis = 0; axis < 3 && hit; ++axis) {
      if (std::fabs(d[axis]) < 1e-8F) {
        hit = o[axis] >= box.mins[axis] && o[axis] <= box.maxs[axis];
        continue;
      }
      float t0 = (box.mins[axis] - o[axis]) / d[axis];
      float t1 = (box.maxs[axis] - o[axis]) / d[axis];
      if (t0 > t1) {
        std::swap(t0, t1);
      }
      tMin = std::max(tMin, t0);
      tMax = std::min(tMax, t1);
      hit = tMin <= tMax;
    }
    if (hit && (!best || tMin < best->distance)) {
      best = HitboxHit{tMin, box.group, start + dir * tMin};
    }
  }
  return best;
}
} // namespace Paper
