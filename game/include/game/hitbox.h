#pragma once

#include "game/movement.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>

// Hit detection geometry (ADR 0011). Damage and shot code only talk to
// `HitboxProvider`; the fixed provider derives boxes from the movement pose,
// a skeletal one can replace it later.
namespace Paper {
enum class HitGroup : std::uint8_t { Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg };

// Damage multiplier of a hit group (the head uses the weapon's own value).
[[nodiscard]] float hitGroupMultiplier(HitGroup group);

// Everything a provider may need to place hitboxes. Extend it (animation
// state) for a skeletal provider.
struct PlayerPose {
  glm::vec3 origin{0.0F};
  // Radians, same convention as UserCmd::yaw.
  float yaw = 0.0F;
  float pitch = 0.0F;
  bool ducked = false;
  bool onGround = true;
  glm::vec3 velocity{0.0F};
};

[[nodiscard]] inline PlayerPose poseOf(const PlayerState &state, float yaw, float pitch) {
  return {state.origin, yaw, pitch, state.ducked, state.onGround, state.velocity};
}

// An axis-aligned box in the player's local frame (origin at the feet, then
// rotated by the pose's yaw).
struct HitboxLocal {
  HitGroup group = HitGroup::Chest;
  glm::vec3 mins{0.0F};
  glm::vec3 maxs{0.0F};
};

inline constexpr std::size_t kMaxHitboxes = 12;

struct HitboxSet {
  glm::vec3 origin{0.0F};
  float yaw = 0.0F;
  std::array<HitboxLocal, kMaxHitboxes> boxes;
  std::size_t count = 0;
};

class HitboxProvider {
public:
  virtual ~HitboxProvider() = default;
  virtual void hitboxes(const PlayerPose &pose, HitboxSet &out) const = 0;
};

// Seven boxes (head, chest, stomach, two arms, two legs) scaled for crouching.
class FixedHitboxProvider final : public HitboxProvider {
public:
  void hitboxes(const PlayerPose &pose, HitboxSet &out) const override;
};

struct HitboxHit {
  float distance = 0.0F;
  HitGroup group = HitGroup::Chest;
  glm::vec3 point{0.0F};
};

// Nearest hitbox hit by the ray (direction must be normalized), within
// `maxDistance`.
[[nodiscard]] std::optional<HitboxHit> rayVsHitboxes(const HitboxSet &set, const glm::vec3 &start,
                                                     const glm::vec3 &dir, float maxDistance);
} // namespace Paper
