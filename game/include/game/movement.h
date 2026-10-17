#pragma once

#include "base/cvar.h"
#include "game/collision.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Paper {
// One tick of a player's input (the "usercmd" of ADR 0009).
struct UserCmd {
  // Radians. Yaw 0 looks along -Z; positive yaw turns left (counter-clockwise
  // seen from above), matching the renderer's camera.
  float yaw = 0.0F;
  float pitch = 0.0F;
  // -1..1: forward / backward and right / left.
  float forward = 0.0F;
  float side = 0.0F;
  bool jump = false;
  bool duck = false;
  // Shift-walk: slower and silent in CS.
  bool walk = false;
  // Combat input (game/weapon.h).
  bool attack = false;
  bool reload = false;
  // 0 = keep, otherwise weapon index + 1.
  std::uint8_t weaponSelect = 0;
  // Server tick (fractional) at which the shooter's screen showed the other
  // players, for lag compensation. 0 = unknown (no rewind).
  float viewTick = 0.0F;
};

// The replicated movement state of one player. `origin` is at the feet.
struct PlayerState {
  glm::vec3 origin{0.0F};
  glm::vec3 velocity{0.0F};
  bool onGround = false;
  bool ducked = false;
  // Jump was held last tick (no auto-bunnyhop: release to jump again).
  bool jumpHeld = false;
};

// Source/CS:GO movement constants (units: Source units, seconds).
struct MovementParams {
  float accelerate = 5.5F;
  float airAccelerate = 12.0F;
  float friction = 5.2F;
  float stopSpeed = 80.0F;
  float gravity = 800.0F;
  // sqrt(2 * 800 * 57): a 57 unit jump.
  float jumpImpulse = 301.993377F;
  float stepSize = 18.0F;
  // Surfaces with normal.y below this are too steep to stand on (surfable).
  float walkableNormal = 0.7F;
  float maxVelocity = 3500.0F;
  // Speed scale while ducked and while shift-walking.
  float duckSpeedFactor = 0.34F;
  float walkSpeedFactor = 0.52F;
  // In air, only this much of the wish speed counts toward acceleration; this
  // is what makes air-strafing work.
  float airSpeedCap = 30.0F;
  // When false (CS default), a jump caps total speed at bunnyJumpCap * maxSpeed,
  // which limits what bunnyhopping can gain on the ground.
  bool enableBunnyhopping = false;
  float bunnyJumpCap = 1.1F;
  // Ducking in mid-air pulls the feet up by this much (crouch-jump).
  float airDuckLift = 9.0F;
};

// cvars for MovementParams, registered on `registry` (sv_accelerate, ...).
class MovementCvars {
public:
  explicit MovementCvars(CvarRegistry &registry = cvars());
  [[nodiscard]] MovementParams params() const;

private:
  Cvar<float> &m_accelerate;
  Cvar<float> &m_airAccelerate;
  Cvar<float> &m_friction;
  Cvar<float> &m_stopSpeed;
  Cvar<float> &m_gravity;
  Cvar<float> &m_jumpImpulse;
  Cvar<float> &m_stepSize;
  Cvar<float> &m_walkableNormal;
  Cvar<float> &m_maxVelocity;
  Cvar<float> &m_duckSpeedFactor;
  Cvar<float> &m_walkSpeedFactor;
  Cvar<float> &m_airSpeedCap;
  Cvar<bool> &m_enableBunnyhopping;
  Cvar<float> &m_bunnyJumpCap;
  Cvar<float> &m_airDuckLift;
};

inline constexpr Hull kStandingHull{{-16.0F, 0.0F, -16.0F}, {16.0F, 72.0F, 16.0F}};
inline constexpr Hull kDuckedHull{{-16.0F, 0.0F, -16.0F}, {16.0F, 54.0F, 16.0F}};
inline constexpr float kStandingEyeHeight = 64.0F;
inline constexpr float kDuckedEyeHeight = 46.0F;
// Knife speed; weapons lower it.
inline constexpr float kDefaultMaxSpeed = 250.0F;

[[nodiscard]] inline const Hull &hullOf(const PlayerState &state) {
  return state.ducked ? kDuckedHull : kStandingHull;
}
[[nodiscard]] inline glm::vec3 eyePosition(const PlayerState &state) {
  return state.origin +
         glm::vec3(0.0F, state.ducked ? kDuckedEyeHeight : kStandingEyeHeight, 0.0F);
}

// Advances `state` by one fixed tick of `dt` seconds. `maxSpeed` is the
// current weapon's running speed.
void movePlayer(PlayerState &state, const UserCmd &cmd, const CollisionWorld &world,
                const MovementParams &params, float maxSpeed, float dt);
} // namespace Paper
