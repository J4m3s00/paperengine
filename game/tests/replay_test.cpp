#include <doctest/doctest.h>

#include "game/map.h"
#include "game/replay.h"
#include "game/tick.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>

using namespace Paper;
using glm::vec3;

namespace {
const MovementParams kParams{};
constexpr float kDt = static_cast<float>(kServerTickSeconds);

void checkNear(const PlayerState &a, const PlayerState &b) {
  CHECK(std::abs(a.origin.x - b.origin.x) < 0.05F);
  CHECK(std::abs(a.origin.y - b.origin.y) < 0.05F);
  CHECK(std::abs(a.origin.z - b.origin.z) < 0.05F);
  CHECK(glm::distance(a.velocity, b.velocity) < 0.05F);
  CHECK(a.onGround == b.onGround);
  CHECK(a.ducked == b.ducked);
}

// Builds a replay by driving the simulation with `policy` for `ticks` ticks,
// recording the commands it chose.
Replay record(const CollisionWorld &world, PlayerState start, int ticks,
              const std::function<UserCmd(int, const PlayerState &)> &policy) {
  Replay replay;
  replay.map = "dev_world.json";
  replay.start = start;
  replay.params = kParams;
  PlayerState state = start;
  for (int i = 0; i < ticks; ++i) {
    const UserCmd cmd = policy(i, state);
    replay.cmds.push_back(cmd);
    movePlayer(state, cmd, world, kParams, kDefaultMaxSpeed, kDt);
  }
  replay.expected = state;
  return replay;
}

PlayerState spawnAt(vec3 origin) {
  PlayerState s;
  s.origin = origin;
  return s;
}

// In the air above the east surf ramp, already moving along the valley.
PlayerState surfStart() {
  PlayerState s = spawnAt({500, 260, -1250});
  s.velocity = {0, 0, -600};
  return s;
}

struct Scenario {
  const char *name;
  PlayerState start;
  int ticks;
  std::function<UserCmd(int, const PlayerState &)> policy;
};

std::vector<Scenario> scenarios() {
  return {
      {"run_up_stairs", spawnAt({0, 2, 0}), 128 * 3,
       [](int, const PlayerState &) {
         UserCmd c;
         c.forward = 1;
         return c;
       }},
      {"crouch_through_tunnel", spawnAt({0, 2, 560}), 128 * 12,
       [](int, const PlayerState &) {
         UserCmd c;
         c.forward = 1;
         c.duck = true;
         c.yaw = glm::radians(180.0F); // toward +Z
         return c;
       }},
      {"hop_onto_boxes", spawnAt({-700, 2, -70}), 128 * 5,
       [](int i, const PlayerState &) {
         UserCmd c;
         c.forward = 1;
         c.yaw = glm::radians(-90.0F); // toward +X... boxes sit at z -100..-40
         c.jump = (i % 60) == 20;
         return c;
       }},
      {"strafe_hop_chain", spawnAt({-1800, 2, 1900}), 128 * 12,
       [yaw = 0.0F, dir = 1, wasGround = true](int, const PlayerState &s) mutable {
         UserCmd c;
         c.forward = 1;
         c.side = static_cast<float>(dir);
         c.jump = s.onGround;
         if (!s.onGround) {
           yaw += -static_cast<float>(dir) * 0.8F * kDt * 3.0F;
         }
         if (wasGround && !s.onGround) {
           dir = -dir;
         }
         wasGround = s.onGround;
         c.yaw = yaw;
         return c;
       }},
      {"surf_ramp_slide", surfStart(), 100,
       [](int, const PlayerState &) {
         UserCmd c;
         c.side = 1; // press into the ramp (it rises toward +X)
         return c;
       }},
  };
}

std::filesystem::path replayPath(const char *name) {
  return std::filesystem::path(PAPER_REPLAYS_DIR) / (std::string(name) + ".replay.json");
}
} // namespace

TEST_CASE("replay serialization round-trips") {
  Replay replay;
  replay.map = "x.json";
  replay.start = spawnAt({1, 2, 3});
  replay.params = kParams;
  replay.params.gravity = 700.0F;
  for (int i = 0; i < 5; ++i) {
    UserCmd c;
    c.yaw = 0.1F * static_cast<float>(i);
    c.pitch = -0.2F;
    c.forward = 1;
    c.side = i % 2 ? 1.0F : -1.0F;
    c.jump = i == 2;
    c.duck = i == 3;
    c.walk = i == 4;
    replay.cmds.push_back(c);
  }
  replay.expected = spawnAt({4, 5, 6});
  const auto parsed = parseReplay(serializeReplay(replay));
  REQUIRE(parsed);
  CHECK(parsed->map == "x.json");
  CHECK(parsed->params.gravity == 700.0F);
  REQUIRE(parsed->cmds.size() == 5);
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK(parsed->cmds[i].yaw == replay.cmds[i].yaw);
    CHECK(parsed->cmds[i].side == replay.cmds[i].side);
    CHECK(parsed->cmds[i].jump == replay.cmds[i].jump);
    CHECK(parsed->cmds[i].duck == replay.cmds[i].duck);
    CHECK(parsed->cmds[i].walk == replay.cmds[i].walk);
  }
  REQUIRE(parsed->expected);
  CHECK(parsed->expected->origin == vec3(4, 5, 6));
}

TEST_CASE("bad replays are rejected") {
  CHECK_FALSE(parseReplay("not json"));
  CHECK_FALSE(parseReplay(R"({ "version": 2 })"));
  CHECK_FALSE(parseReplay(R"({ "version": 1, "tickRate": 64 })"));
}

#ifdef PAPER_MAPS_DIR
// Golden regression tests: each scenario's commands and final state are stored
// in game/tests/replays. After an intentional movement change, re-bless them:
//   PAPER_UPDATE_REPLAYS=1 ./build/debug/game/game_tests -tc="replay golden*"
TEST_CASE("replay golden: movement scenarios on the dev world") {
  auto map = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json");
  REQUIRE(map);
  auto world = map->buildCollision();
  REQUIRE(world);
  const bool update = std::getenv("PAPER_UPDATE_REPLAYS") != nullptr;

  for (const Scenario &scenario : scenarios()) {
    CAPTURE(scenario.name);
    if (update) {
      const Replay replay = record(*world, scenario.start, scenario.ticks, scenario.policy);
      REQUIRE(saveReplay(replayPath(scenario.name), replay));
      continue;
    }
    const auto replay = loadReplay(replayPath(scenario.name));
    REQUIRE_MESSAGE(replay.has_value(), "missing golden replay; run with PAPER_UPDATE_REPLAYS=1");
    REQUIRE(replay->expected);
    const PlayerState end = runReplay(*replay, *world);
    checkNear(end, *replay->expected);
    // The replay must also be self-consistent when run twice.
    checkNear(runReplay(*replay, *world), end);
  }
}

TEST_CASE("replay trajectory has one state per tick") {
  auto map = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json");
  REQUIRE(map);
  auto world = map->buildCollision();
  REQUIRE(world);
  const Replay replay = record(*world, spawnAt({0, 2, 0}), 100, [](int, const PlayerState &) {
    UserCmd c;
    c.forward = 1;
    return c;
  });
  std::vector<PlayerState> trajectory;
  (void)runReplay(replay, *world, &trajectory);
  CHECK(trajectory.size() == 100);
}
#endif


#ifdef PAPER_MAPS_DIR
TEST_CASE("replay golden scenarios do what they say") {
  auto map = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json");
  REQUIRE(map);
  auto world = map->buildCollision();
  REQUIRE(world);
  const auto end = [&](const char *name) {
    auto replay = loadReplay(replayPath(name));
    REQUIRE(replay.has_value());
    return runReplay(*replay, *world);
  };
  // Climbed the stairs: standing on the platform 120 units up.
  CHECK(end("run_up_stairs").origin.y > 100.0F);
  // Crouched through the tunnel (ceiling at 56) and out the far side.
  CHECK(end("crouch_through_tunnel").origin.z > 1010.0F);
  // Hopped up onto at least one of the jump boxes (top 18..80).
  CHECK(end("hop_onto_boxes").origin.x > -560.0F);
  // Strafe-hopping gained speed beyond the 250 ground maximum.
  const PlayerState hop = end("strafe_hop_chain");
  CHECK(std::hypot(hop.velocity.x, hop.velocity.z) > 270.0F);
  // Still riding the ramp at speed: never standing (too steep) and never
  // wedged against the surface (regression: repeated plane zeroed velocity).
  const PlayerState surf = end("surf_ramp_slide");
  CHECK_FALSE(surf.onGround);
  CHECK(std::hypot(surf.velocity.x, surf.velocity.z) > 500.0F);
}
#endif
