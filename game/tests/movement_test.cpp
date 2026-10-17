#include <doctest/doctest.h>

#include "game/movement.h"

#include <cmath>

using namespace Paper;
using glm::vec3;

namespace {
constexpr float kDt = 1.0F / 128.0F;
const MovementParams kParams{};

CollisionWorld makeWorld(const std::vector<BrushDesc> &brushes) {
  auto world = CollisionWorld::create(brushes);
  REQUIRE(world.has_value());
  return std::move(*world);
}

BrushDesc floorBrush() { return boxBrush({-5000, -100, -5000}, {5000, 0, 5000}); }

// Runs `ticks` ticks of the same command.
void run(PlayerState &s, const UserCmd &cmd, const CollisionWorld &w, int ticks,
         const MovementParams &p = kParams, float maxSpeed = kDefaultMaxSpeed) {
  for (int i = 0; i < ticks; ++i) {
    movePlayer(s, cmd, w, p, maxSpeed, kDt);
  }
}

// Spawns a few units above the floor (a hull exactly touching a surface counts
// as starting inside it) and lets it settle.
PlayerState standing(const CollisionWorld &w, vec3 at = {0, 4, 0}) {
  PlayerState s;
  s.origin = at;
  run(s, {}, w, 64);
  return s;
}

float horizontalSpeed(const PlayerState &s) { return std::hypot(s.velocity.x, s.velocity.z); }

UserCmd forward() {
  UserCmd c;
  c.forward = 1.0F;
  return c;
}
} // namespace

TEST_CASE("standing still on the floor stays put and grounded") {
  const auto world = makeWorld({floorBrush()});
  const PlayerState s = standing(world);
  CHECK(s.onGround);
  CHECK(s.origin.y < 2.1F); // within the 2 unit ground probe
  CHECK(horizontalSpeed(s) == 0.0F);
}

TEST_CASE("falling lands on the floor") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s;
  s.origin = {0, 100, 0};
  run(s, {}, world, 128);
  CHECK(s.onGround);
  CHECK(s.origin.y == doctest::Approx(0.0F).epsilon(0.3));
  CHECK(s.velocity.y == 0.0F);
}

TEST_CASE("running reaches max speed along yaw") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  run(s, forward(), world, 256);
  CHECK(horizontalSpeed(s) == doctest::Approx(250.0F).epsilon(0.001));
  // Yaw 0 looks along -Z.
  CHECK(s.velocity.z < 0.0F);
  CHECK(std::abs(s.velocity.x) < 0.01F);
}

TEST_CASE("diagonal running is no faster than straight") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  UserCmd cmd = forward();
  cmd.side = 1.0F;
  run(s, cmd, world, 256);
  CHECK(horizontalSpeed(s) == doctest::Approx(250.0F).epsilon(0.001));
}

TEST_CASE("crouching and walking are slower") {
  const auto world = makeWorld({floorBrush()});
  PlayerState duck = standing(world);
  UserCmd cmd = forward();
  cmd.duck = true;
  run(duck, cmd, world, 1024);
  CHECK(duck.ducked);
  CHECK(horizontalSpeed(duck) == doctest::Approx(250.0F * 0.34F).epsilon(0.001));

  PlayerState walk = standing(world);
  cmd = forward();
  cmd.walk = true;
  run(walk, cmd, world, 512);
  CHECK(horizontalSpeed(walk) == doctest::Approx(250.0F * 0.52F).epsilon(0.001));
}

TEST_CASE("friction stops a sliding player") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  s.velocity = {0, 0, -250};
  run(s, {}, world, 128);
  CHECK(horizontalSpeed(s) == 0.0F);
}

TEST_CASE("a jump reaches about 57 units") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  UserCmd cmd;
  cmd.jump = true;
  const float base = s.origin.y;
  float peak = base;
  for (int i = 0; i < 128; ++i) {
    movePlayer(s, cmd, world, kParams, kDefaultMaxSpeed, kDt);
    peak = std::max(peak, s.origin.y);
  }
  CHECK(peak - base > 56.0F);
  CHECK(peak - base < 58.0F);
}

TEST_CASE("holding jump does not bunnyhop automatically") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  UserCmd cmd;
  cmd.jump = true;
  int jumps = 0;
  bool wasGround = true;
  for (int i = 0; i < 400; ++i) {
    movePlayer(s, cmd, world, kParams, kDefaultMaxSpeed, kDt);
    if (wasGround && !s.onGround) {
      ++jumps;
    }
    wasGround = s.onGround;
  }
  CHECK(jumps == 1);
}

TEST_CASE("releasing and pressing jump again hops again") {
  const auto world = makeWorld({floorBrush()});
  PlayerState s = standing(world);
  int jumps = 0;
  bool wasGround = true;
  for (int i = 0; i < 400; ++i) {
    UserCmd cmd;
    cmd.jump = s.onGround ? (i % 2 == 0) : false;
    movePlayer(s, cmd, world, kParams, kDefaultMaxSpeed, kDt);
    if (wasGround && !s.onGround) {
      ++jumps;
    }
    wasGround = s.onGround;
  }
  CHECK(jumps > 3);
}

TEST_CASE("jumping caps speed unless bunnyhopping is enabled") {
  const auto world = makeWorld({floorBrush()});
  UserCmd cmd;
  cmd.jump = true;

  PlayerState capped = standing(world);
  capped.velocity = {0, 0, -400};
  movePlayer(capped, cmd, world, kParams, kDefaultMaxSpeed, kDt);
  CHECK(horizontalSpeed(capped) == doctest::Approx(275.0F).epsilon(0.01));

  MovementParams free = kParams;
  free.enableBunnyhopping = true;
  PlayerState uncapped = standing(world);
  uncapped.velocity = {0, 0, -400};
  movePlayer(uncapped, cmd, world, free, kDefaultMaxSpeed, kDt);
  CHECK(horizontalSpeed(uncapped) > 380.0F);
}

TEST_CASE("air-strafing gains speed beyond the ground maximum") {
  const auto world = makeWorld({floorBrush()});
  MovementParams p = kParams;
  p.gravity = 0.0F; // stay in the air long enough to measure
  PlayerState s;
  s.origin = {0, 100, 0};
  s.velocity = {0, 0, -250};
  // Hold "right" while turning the view right so the wish direction stays
  // perpendicular to the velocity.
  float yaw = 0.0F;
  const float startSpeed = horizontalSpeed(s);
  for (int i = 0; i < 100; ++i) {
    UserCmd cmd;
    cmd.side = 1.0F;
    cmd.yaw = yaw;
    movePlayer(s, cmd, world, p, kDefaultMaxSpeed, kDt);
    // Turn right (negative yaw) toward the velocity's perpendicular.
    yaw -= 0.8F * kDt * 6.0F;
  }
  CHECK_FALSE(s.onGround);
  CHECK(horizontalSpeed(s) > startSpeed + 40.0F);
}

TEST_CASE("walking up a flight of stairs") {
  std::vector<BrushDesc> brushes{floorBrush()};
  // 10 steps, 8 high and 20 deep, going toward -Z.
  for (int i = 0; i < 10; ++i) {
    const float z = -100.0F - 20.0F * static_cast<float>(i);
    brushes.push_back(boxBrush({-200, 0, z - 20.0F}, {200, 8.0F * static_cast<float>(i + 1), z}));
  }
  brushes.push_back(boxBrush({-200, 0, -2000}, {200, 80, -300})); // landing at the top
  const auto world = makeWorld(brushes);
  PlayerState s = standing(world);
  run(s, forward(), world, 128 * 2);
  CHECK(s.onGround);
  CHECK(s.origin.y == doctest::Approx(80.0F).epsilon(0.02));
}

TEST_CASE("a ledge higher than the step size blocks the player") {
  const auto world = makeWorld({floorBrush(), boxBrush({-200, 0, -300}, {200, 30, -100})});
  PlayerState s = standing(world);
  run(s, forward(), world, 128 * 2);
  CHECK(s.origin.y == doctest::Approx(0.0F).epsilon(0.2));
  CHECK(s.origin.z > -100.0F - 16.0F - 1.0F);
}

TEST_CASE("running along a wall at an angle keeps the parallel speed") {
  const auto world = makeWorld({floorBrush(), boxBrush({-200, 0, -5000}, {-50, 200, 5000})});
  PlayerState s = standing(world);
  UserCmd cmd;
  cmd.forward = 1.0F;
  cmd.side = -1.0F; // forward-left, into the wall
  run(s, cmd, world, 128 * 2);
  // The wall is at x = -50: stopped there (hull half width 16), still moving.
  CHECK(s.origin.x == doctest::Approx(-34.0F).epsilon(0.02));
  CHECK(s.velocity.z < -150.0F);
}

TEST_CASE("walking up a gentle slope stays grounded") {
  const float s30 = 0.5F;
  const float c30 = std::sqrt(0.75F);
  // Ramp rising toward -Z at 30 degrees: surface normal (0, c30, s30).
  BrushDesc ramp{{{{0, -1, 0}, 100.0F}, {{0, c30, s30}, 0.0F}, {{0, 0, 1}, 100.0F},
                  {{1, 0, 0}, 300.0F}, {{-1, 0, 0}, 300.0F}, {{0, 0, -1}, 2000.0F}}};
  const auto world = makeWorld({ramp});
  PlayerState s;
  s.origin = {0, 30, 0}; // drop onto the ramp
  run(s, {}, world, 128);
  REQUIRE(s.onGround);
  bool everAirborne = false;
  for (int i = 0; i < 128; ++i) {
    movePlayer(s, forward(), world, kParams, kDefaultMaxSpeed, kDt);
    everAirborne = everAirborne || !s.onGround;
  }
  CHECK_FALSE(everAirborne);
  CHECK(s.origin.y > 20.0F);
}

TEST_CASE("a steep ramp cannot be stood on (surf)") {
  const float s60 = std::sqrt(0.75F);
  const float c60 = 0.5F;
  // Normal (0, 0.5, 0.866): steeper than walkable (0.7).
  BrushDesc ramp{{{{0, -1, 0}, 100.0F}, {{0, c60, s60}, 0.0F}, {{0, 0, 1}, 100.0F},
                  {{1, 0, 0}, 300.0F}, {{-1, 0, 0}, 300.0F}, {{0, 0, -1}, 5000.0F}}};
  const auto world = makeWorld({ramp});
  PlayerState s;
  s.origin = {0, 120, -200};
  s.velocity = {0, 0, 200};
  bool everGrounded = false;
  for (int i = 0; i < 40; ++i) {
    movePlayer(s, {}, world, kParams, kDefaultMaxSpeed, kDt);
    everGrounded = everGrounded || s.onGround;
  }
  CHECK_FALSE(everGrounded);
}

TEST_CASE("crouch-standing needs headroom") {
  const auto world = makeWorld({floorBrush(), boxBrush({-100, 60, -100}, {100, 100, 100})});
  PlayerState s = standing(world); // standing hull is 72 tall: the ceiling at 60 is inside it
  // Spawn crouched under the low ceiling instead.
  PlayerState c;
  c.ducked = true;
  c.origin = {0, 1, 0};
  UserCmd cmd;
  cmd.duck = true;
  run(c, cmd, world, 8);
  CHECK(c.ducked);
  cmd.duck = false;
  run(c, cmd, world, 8);
  CHECK(c.ducked); // cannot stand up: ceiling is 60 above the floor
  (void)s;
}

TEST_CASE("movement is deterministic") {
  const auto world = makeWorld({floorBrush()});
  const auto simulate = [&] {
    PlayerState s = standing(world);
    for (int i = 0; i < 500; ++i) {
      UserCmd cmd;
      cmd.forward = (i % 7) < 5 ? 1.0F : 0.0F;
      cmd.side = (i % 11) < 4 ? 1.0F : -1.0F;
      cmd.jump = (i % 40) == 3;
      cmd.yaw = 0.01F * static_cast<float>(i);
      movePlayer(s, cmd, world, kParams, kDefaultMaxSpeed, kDt);
    }
    return s;
  };
  const PlayerState a = simulate();
  const PlayerState b = simulate();
  CHECK(a.origin == b.origin);
  CHECK(a.velocity == b.velocity);
}

TEST_CASE("movement cvars match the default params") {
  CvarRegistry registry;
  const MovementCvars cvars(registry);
  const MovementParams p = cvars.params();
  CHECK(p.accelerate == kParams.accelerate);
  CHECK(p.airAccelerate == kParams.airAccelerate);
  CHECK(p.gravity == kParams.gravity);
  CHECK(p.enableBunnyhopping == kParams.enableBunnyhopping);
  CHECK(registry.find("sv_airaccelerate") != nullptr);
}

TEST_CASE("counter-strafing stops much faster than releasing the key") {
  const auto world = makeWorld({floorBrush()});
  const auto ticksToStop = [&](const UserCmd &cmd, bool untilReversed) {
    PlayerState s = standing(world);
    s.velocity = {0, 0, -250};
    int ticks = 0;
    while (ticks < 1000 && (untilReversed ? s.velocity.z < 0.0F : horizontalSpeed(s) > 0.0F)) {
      run(s, cmd, world, 1);
      ++ticks;
    }
    return ticks;
  };
  UserCmd back;
  back.forward = -1.0F;
  const int released = ticksToStop({}, false);
  const int countered = ticksToStop(back, true);
  CHECK(released > 45);
  CHECK(countered < 20);
  CHECK(countered * 3 < released);
}
