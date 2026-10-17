#include "game/lag_compensation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Paper {
namespace {
// A jump this large between two ticks is a teleport (respawn), not movement.
constexpr float kTeleportDistance = 256.0F;

float lerpAngle(float a, float b, float t) {
  constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
  float d = std::fmod(b - a, twoPi);
  if (d > std::numbers::pi_v<float>) d -= twoPi;
  if (d < -std::numbers::pi_v<float>) d += twoPi;
  return a + d * t;
}
} // namespace

void LagCompensation::record(std::uint32_t tick, std::uint8_t slot, const PlayerPose &pose) {
  m_history[slot][tick % kHistoryTicks] = {tick, true, pose};
}

void LagCompensation::clear(std::uint8_t slot) {
  for (Entry &e : m_history[slot]) {
    e.valid = false;
  }
}

const LagCompensation::Entry *LagCompensation::entry(std::uint8_t slot, std::uint32_t tick) const {
  const Entry &e = m_history[slot][tick % kHistoryTicks];
  return e.valid && e.tick == tick ? &e : nullptr;
}

std::optional<PlayerPose> LagCompensation::poseAt(std::uint8_t slot, double tick) const {
  if (slot >= kSlots || tick < 0.0) {
    return std::nullopt;
  }
  const auto t0 = static_cast<std::uint32_t>(std::floor(tick));
  const double frac = tick - static_cast<double>(t0);
  const Entry *a = entry(slot, t0);
  const Entry *b = frac > 0.0 ? entry(slot, t0 + 1) : nullptr;
  if (!a && !b) {
    return std::nullopt;
  }
  if (!b) {
    return a->pose;
  }
  if (!a) {
    return b->pose;
  }
  if (glm::distance(a->pose.origin, b->pose.origin) > kTeleportDistance) {
    return frac < 0.5 ? a->pose : b->pose;
  }
  const auto t = static_cast<float>(frac);
  PlayerPose p = frac < 0.5 ? a->pose : b->pose;
  p.origin = glm::mix(a->pose.origin, b->pose.origin, t);
  p.velocity = glm::mix(a->pose.velocity, b->pose.velocity, t);
  p.yaw = lerpAngle(a->pose.yaw, b->pose.yaw, t);
  p.pitch = lerpAngle(a->pose.pitch, b->pose.pitch, t);
  return p;
}

double LagCompensation::clampViewTick(double viewTick, std::uint32_t currentTick,
                                      std::uint32_t maxRewindTicks) {
  const double newest = static_cast<double>(currentTick);
  const double oldest = std::max(0.0, newest - static_cast<double>(std::min<std::uint32_t>(
                                           maxRewindTicks, kHistoryTicks - 2)));
  return std::clamp(viewTick, oldest, newest);
}
} // namespace Paper
