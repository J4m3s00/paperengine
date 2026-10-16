#include "game/movement.h"

#include <algorithm>
#include <array>
#include <cmath>

// A port of the Source engine's walking movement (CGameMovement::FullWalkMove),
// adapted to Y up. Order of operations matters for the feel: gravity is applied
// half before and half after the move, friction only while grounded, and a
// jump leaves the ground before friction runs (which is what makes bunnyhops
// frictionless).
namespace Paper {
MovementCvars::MovementCvars(CvarRegistry &r)
    : m_accelerate(r.add<float>("sv_accelerate", 5.5F, 0.0F, 100.0F, "Ground acceleration")),
      m_airAccelerate(r.add<float>("sv_airaccelerate", 12.0F, 0.0F, 1000.0F, "Air acceleration")),
      m_friction(r.add<float>("sv_friction", 5.2F, 0.0F, 50.0F, "Ground friction")),
      m_stopSpeed(r.add<float>("sv_stopspeed", 80.0F, 0.0F, 1000.0F, "Friction control floor")),
      m_gravity(r.add<float>("sv_gravity", 800.0F, 0.0F, 5000.0F, "Gravity (units/s^2)")),
      m_jumpImpulse(r.add<float>("sv_jump_impulse", 301.993377F, 0.0F, 2000.0F,
                                 "Upward speed of a jump")),
      m_stepSize(r.add<float>("sv_stepsize", 18.0F, 0.0F, 100.0F, "Max step height")),
      m_walkableNormal(r.add<float>("sv_walkable_normal", 0.7F, 0.0F, 1.0F,
                                    "Min ground normal.y to stand on")),
      m_maxVelocity(r.add<float>("sv_maxvelocity", 3500.0F, 1.0F, 100000.0F,
                                 "Per-axis speed limit")),
      m_duckSpeedFactor(r.add<float>("sv_duck_speed_factor", 0.34F, 0.0F, 1.0F,
                                     "Speed scale while crouched")),
      m_walkSpeedFactor(r.add<float>("sv_walk_speed_factor", 0.52F, 0.0F, 1.0F,
                                     "Speed scale while shift-walking")),
      m_airSpeedCap(r.add<float>("sv_air_speed_cap", 30.0F, 0.0F, 1000.0F,
                                 "Air-strafe wish speed cap")),
      m_enableBunnyhopping(r.addBool("sv_enablebunnyhopping", false,
                                     "Allow unlimited speed gain from bunnyhopping")),
      m_bunnyJumpCap(r.add<float>("sv_bunnyjump_cap", 1.1F, 1.0F, 10.0F,
                                  "Jump speed cap as a factor of max speed")),
      m_airDuckLift(r.add<float>("sv_airduck_lift", 9.0F, 0.0F, 50.0F,
                                 "Feet raise when crouching in the air")) {}

MovementParams MovementCvars::params() const {
  MovementParams p;
  p.accelerate = m_accelerate.value();
  p.airAccelerate = m_airAccelerate.value();
  p.friction = m_friction.value();
  p.stopSpeed = m_stopSpeed.value();
  p.gravity = m_gravity.value();
  p.jumpImpulse = m_jumpImpulse.value();
  p.stepSize = m_stepSize.value();
  p.walkableNormal = m_walkableNormal.value();
  p.maxVelocity = m_maxVelocity.value();
  p.duckSpeedFactor = m_duckSpeedFactor.value();
  p.walkSpeedFactor = m_walkSpeedFactor.value();
  p.airSpeedCap = m_airSpeedCap.value();
  p.enableBunnyhopping = m_enableBunnyhopping.value();
  p.bunnyJumpCap = m_bunnyJumpCap.value();
  p.airDuckLift = m_airDuckLift.value();
  return p;
}

namespace {
// cmd.forward/side are -1..1; Source's keyboard move speed (cl_forwardspeed)
// is 450, then clamped to the weapon's max speed, so diagonals keep direction.
constexpr float kCmdMoveScale = 450.0F;
// Moving up faster than this leaves the ground (ramp launches, jumps).
constexpr float kNonJumpVelocity = 140.0F;
// Ground probe distance.
constexpr float kGroundProbe = 2.0F;
// Clip velocity slightly away from surfaces so float error cannot leave it
// pointing into one (Quake 3's OVERCLIP).
constexpr float kOverClip = 1.001F;
constexpr int kMaxBumps = 4;
constexpr int kMaxClipPlanes = 5;

struct Ctx {
  PlayerState &s;
  const CollisionWorld &world;
  const MovementParams &p;
  float dt;
};

TraceResult trace(const Ctx &c, const glm::vec3 &from, const glm::vec3 &to) {
  return c.world.traceHull(from, to, hullOf(c.s));
}

// Removes the part of `in` that points into the surface.
glm::vec3 clipVelocity(const glm::vec3 &in, const glm::vec3 &normal, float overbounce) {
  const float backoff = glm::dot(in, normal) * overbounce;
  glm::vec3 out = in - normal * backoff;
  // Rounding can leave a small component into the plane; remove it.
  const float adjust = glm::dot(out, normal);
  if (adjust < 0.0F) {
    out -= normal * adjust;
  }
  return out;
}

void checkVelocity(const Ctx &c) {
  c.s.velocity = glm::clamp(c.s.velocity, glm::vec3(-c.p.maxVelocity), glm::vec3(c.p.maxVelocity));
}

void startGravity(const Ctx &c) { c.s.velocity.y -= c.p.gravity * 0.5F * c.dt; }
void finishGravity(const Ctx &c) { c.s.velocity.y -= c.p.gravity * 0.5F * c.dt; }

// Flat forward/right unit vectors from yaw (see UserCmd::yaw).
void basis(const UserCmd &cmd, glm::vec3 &forward, glm::vec3 &right) {
  forward = glm::vec3(-std::sin(cmd.yaw), 0.0F, -std::cos(cmd.yaw));
  right = glm::vec3(std::cos(cmd.yaw), 0.0F, -std::sin(cmd.yaw));
}

float speedLimit(const Ctx &c, float maxSpeed, const UserCmd &cmd) {
  if (c.s.ducked) {
    maxSpeed *= c.p.duckSpeedFactor;
  } else if (cmd.walk) {
    maxSpeed *= c.p.walkSpeedFactor;
  }
  return maxSpeed;
}

void friction(const Ctx &c) {
  const float speed = glm::length(c.s.velocity);
  if (speed < 0.1F) {
    return;
  }
  const float control = std::max(speed, c.p.stopSpeed);
  const float drop = control * c.p.friction * c.dt;
  const float newSpeed = std::max(speed - drop, 0.0F);
  c.s.velocity *= newSpeed / speed;
}

void accelerate(const Ctx &c, const glm::vec3 &wishDir, float wishSpeed, float accel) {
  const float current = glm::dot(c.s.velocity, wishDir);
  const float addSpeed = wishSpeed - current;
  if (addSpeed <= 0.0F) {
    return;
  }
  const float accelSpeed = std::min(accel * c.dt * wishSpeed, addSpeed);
  c.s.velocity += wishDir * accelSpeed;
}

void airAccelerate(const Ctx &c, const glm::vec3 &wishDir, float wishSpeed, float accel) {
  // Only a small slice of the wish speed counts toward the "already going this
  // fast" check, but the acceleration amount uses the full wish speed.
  const float cappedWish = std::min(wishSpeed, c.p.airSpeedCap);
  const float current = glm::dot(c.s.velocity, wishDir);
  const float addSpeed = cappedWish - current;
  if (addSpeed <= 0.0F) {
    return;
  }
  const float accelSpeed = std::min(accel * wishSpeed * c.dt, addSpeed);
  c.s.velocity += wishDir * accelSpeed;
}

// Slides the hull along surfaces for the whole tick, clipping velocity against
// up to kMaxClipPlanes planes (Source's TryPlayerMove).
void tryPlayerMove(const Ctx &c) {
  PlayerState &s = c.s;
  const glm::vec3 primalVelocity = s.velocity;
  glm::vec3 originalVelocity = s.velocity;
  std::array<glm::vec3, kMaxClipPlanes> planes;
  int numPlanes = 0;
  float timeLeft = c.dt;
  float allFraction = 0.0F;

  for (int bump = 0; bump < kMaxBumps; ++bump) {
    if (glm::dot(s.velocity, s.velocity) == 0.0F) {
      break;
    }
    const glm::vec3 end = s.origin + s.velocity * timeLeft;
    const TraceResult t = trace(c, s.origin, end);
    allFraction += t.fraction;

    if (t.allSolid) {
      // Stuck in a solid: do not accumulate velocity.
      s.velocity = glm::vec3(0.0F);
      return;
    }
    if (t.fraction > 0.0F) {
      s.origin = t.endPos;
      originalVelocity = s.velocity;
      numPlanes = 0;
    }
    if (t.fraction == 1.0F) {
      break;
    }
    timeLeft -= timeLeft * t.fraction;

    if (numPlanes >= kMaxClipPlanes) {
      s.velocity = glm::vec3(0.0F);
      break;
    }
    // Hitting a plane we already clipped against means rounding left us moving
    // slightly into it: nudge away instead of treating it as a crease (two equal
    // planes have a zero cross product and would stop the player dead).
    bool repeated = false;
    for (int p = 0; p < numPlanes; ++p) {
      if (glm::dot(t.normal, planes[p]) > 0.99F) {
        s.velocity += t.normal;
        repeated = true;
        break;
      }
    }
    if (repeated) {
      continue;
    }
    planes[numPlanes++] = t.normal;

    // Find a velocity that runs parallel to every plane touched this tick.
    int i = 0;
    glm::vec3 clipped(0.0F);
    for (; i < numPlanes; ++i) {
      clipped = clipVelocity(originalVelocity, planes[i], kOverClip);
      int j = 0;
      for (; j < numPlanes; ++j) {
        if (j != i && glm::dot(clipped, planes[j]) < 0.0F) {
          break;
        }
      }
      if (j == numPlanes) {
        break;
      }
    }
    if (i != numPlanes) {
      s.velocity = clipped;
    } else {
      // Wedged into a crease: slide along its line, or stop.
      if (numPlanes != 2) {
        s.velocity = glm::vec3(0.0F);
        break;
      }
      const glm::vec3 dir = glm::cross(planes[0], planes[1]);
      s.velocity = dir * glm::dot(dir, s.velocity);
    }
    // Never turn back against where we were going.
    if (glm::dot(s.velocity, primalVelocity) <= 0.0F) {
      s.velocity = glm::vec3(0.0F);
      break;
    }
  }
  if (allFraction == 0.0F) {
    s.velocity = glm::vec3(0.0F);
  }
}

// Walk up steps: try the move both directly and raised by the step height,
// and keep whichever got farther.
void stepMove(const Ctx &c) {
  PlayerState &s = c.s;
  const glm::vec3 startPos = s.origin;
  const glm::vec3 startVel = s.velocity;

  tryPlayerMove(c);
  const glm::vec3 downPos = s.origin;
  const glm::vec3 downVel = s.velocity;

  s.origin = startPos;
  s.velocity = startVel;

  const TraceResult up = trace(c, s.origin, s.origin + glm::vec3(0.0F, c.p.stepSize, 0.0F));
  if (!up.startSolid && !up.allSolid) {
    s.origin = up.endPos;
  }
  tryPlayerMove(c);

  const TraceResult down = trace(c, s.origin, s.origin - glm::vec3(0.0F, c.p.stepSize, 0.0F));
  if (!down.startSolid && !down.allSolid) {
    s.origin = down.endPos;
  }
  // Landed on something too steep to stand on: the plain move was right.
  if (down.fraction < 1.0F && down.normal.y < c.p.walkableNormal) {
    s.origin = downPos;
    s.velocity = downVel;
    return;
  }

  const glm::vec3 upPos = s.origin;
  const auto horizontalDistSq = [&](const glm::vec3 &p) {
    const glm::vec3 d = p - startPos;
    return d.x * d.x + d.z * d.z;
  };
  if (horizontalDistSq(downPos) > horizontalDistSq(upPos)) {
    s.origin = downPos;
    s.velocity = downVel;
  } else {
    // Keep the step's position but the plain move's vertical speed.
    s.velocity.y = downVel.y;
  }
}

// Keeps the player glued to the floor when walking down slopes and stairs.
void stayOnGround(const Ctx &c) {
  const glm::vec3 start = c.s.origin + glm::vec3(0.0F, kGroundProbe, 0.0F);
  const glm::vec3 end = c.s.origin - glm::vec3(0.0F, c.p.stepSize, 0.0F);
  const TraceResult t = trace(c, start, end);
  if (t.fraction > 0.0F && t.fraction < 1.0F && !t.startSolid &&
      t.normal.y >= c.p.walkableNormal && std::abs(c.s.origin.y - t.endPos.y) > 0.015F) {
    c.s.origin = t.endPos;
  }
}

void categorizePosition(const Ctx &c) {
  PlayerState &s = c.s;
  if (s.velocity.y > kNonJumpVelocity) {
    s.onGround = false;
    return;
  }
  const TraceResult t = trace(c, s.origin, s.origin - glm::vec3(0.0F, kGroundProbe, 0.0F));
  s.onGround = t.fraction < 1.0F && !t.startSolid && t.normal.y >= c.p.walkableNormal;
}

// Wish direction and speed from the command, clamped to `maxSpeed`.
void wish(const UserCmd &cmd, float maxSpeed, glm::vec3 &dir, float &speed) {
  glm::vec3 forward;
  glm::vec3 right;
  basis(cmd, forward, right);
  glm::vec3 vel = (forward * cmd.forward + right * cmd.side) * kCmdMoveScale;
  speed = glm::length(vel);
  if (speed > 1e-4F) {
    dir = vel / speed;
  } else {
    dir = glm::vec3(0.0F);
    speed = 0.0F;
  }
  speed = std::min(speed, maxSpeed);
}

void walkMove(const Ctx &c, const UserCmd &cmd, float maxSpeed) {
  PlayerState &s = c.s;
  glm::vec3 dir;
  float speed;
  wish(cmd, maxSpeed, dir, speed);

  s.velocity.y = 0.0F;
  accelerate(c, dir, speed, c.p.accelerate);
  s.velocity.y = 0.0F;

  if (glm::length(s.velocity) < 1.0F) {
    s.velocity = glm::vec3(0.0F);
    return;
  }
  const glm::vec3 dest = s.origin + s.velocity * c.dt;
  const TraceResult t = trace(c, s.origin, dest);
  if (t.fraction == 1.0F) {
    s.origin = t.endPos;
    stayOnGround(c);
    return;
  }
  stepMove(c);
  stayOnGround(c);
}

void airMove(const Ctx &c, const UserCmd &cmd, float maxSpeed) {
  glm::vec3 dir;
  float speed;
  wish(cmd, maxSpeed, dir, speed);
  airAccelerate(c, dir, speed, c.p.airAccelerate);
  tryPlayerMove(c);
}

void checkJump(const Ctx &c, bool jump, float maxSpeed) {
  PlayerState &s = c.s;
  if (!jump) {
    s.jumpHeld = false;
    return;
  }
  if (!s.onGround) {
    // Holding jump in the air must not bunnyhop on landing.
    s.jumpHeld = true;
    return;
  }
  if (s.jumpHeld) {
    return;
  }
  if (!c.p.enableBunnyhopping) {
    const float cap = c.p.bunnyJumpCap * maxSpeed;
    const float speed = glm::length(s.velocity);
    if (cap > 0.0F && speed > cap) {
      s.velocity *= cap / speed;
    }
  }
  s.onGround = false;
  s.velocity.y = c.p.jumpImpulse;
  finishGravity(c);
  s.jumpHeld = true;
}

// Crouch / uncrouch. Standing up needs room; in the air crouching lifts the
// feet and standing up drops them back.
void updateDuck(const Ctx &c, bool wantDuck) {
  PlayerState &s = c.s;
  if (wantDuck && !s.ducked) {
    s.ducked = true;
    if (!s.onGround) {
      const glm::vec3 lifted = s.origin + glm::vec3(0.0F, c.p.airDuckLift, 0.0F);
      if (!c.world.traceHull(lifted, lifted, kDuckedHull).startSolid) {
        s.origin = lifted;
      }
    }
  } else if (!wantDuck && s.ducked) {
    glm::vec3 target = s.origin;
    if (!s.onGround) {
      target.y -= c.p.airDuckLift;
      if (c.world.traceHull(target, target, kStandingHull).startSolid) {
        target = s.origin;
      }
    }
    if (!c.world.traceHull(target, target, kStandingHull).startSolid) {
      s.ducked = false;
      s.origin = target;
    }
  }
}
} // namespace

void movePlayer(PlayerState &state, const UserCmd &cmd, const CollisionWorld &world,
                const MovementParams &params, float maxSpeed, float dt) {
  const Ctx c{state, world, params, dt};
  updateDuck(c, cmd.duck);
  const float limit = speedLimit(c, maxSpeed, cmd);

  startGravity(c);
  checkJump(c, cmd.jump, maxSpeed);
  if (state.onGround) {
    state.velocity.y = 0.0F;
    friction(c);
  }
  checkVelocity(c);

  if (state.onGround) {
    walkMove(c, cmd, limit);
  } else {
    airMove(c, cmd, limit);
  }

  categorizePosition(c);
  checkVelocity(c);
  finishGravity(c);
  if (state.onGround) {
    state.velocity.y = 0.0F;
  }
}
} // namespace Paper
