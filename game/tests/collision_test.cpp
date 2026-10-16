#include <doctest/doctest.h>

#include "game/collision.h"

#include <chrono>
#include <cmath>
#include <cstdint>

using namespace Paper;
using glm::vec3;

namespace {
constexpr Hull kPlayer{{-16, 0, -16}, {16, 72, 16}};
constexpr float kEps = 0.2F; // surface epsilon is 0.125

CollisionWorld makeWorld(const std::vector<BrushDesc> &brushes) {
  auto world = CollisionWorld::create(brushes);
  REQUIRE(world.has_value());
  return std::move(*world);
}

// A diamond-shaped pillar: |x| + |z| <= 10, y in [-50, 50].
BrushDesc diamond() {
  const float s = std::sqrt(0.5F);
  const float d = 10.0F * s;
  return {{{{s, 0, s}, d}, {{s, 0, -s}, d}, {{-s, 0, s}, d}, {{-s, 0, -s}, d},
           {{0, 1, 0}, 50}, {{0, -1, 0}, 50}}};
}
} // namespace

TEST_CASE("empty world never collides") {
  const auto world = makeWorld({});
  const auto trace = world.traceHull({0, 0, 0}, {100, 0, 0}, kPlayer);
  CHECK(trace.fraction == 1.0F);
  CHECK(trace.endPos == vec3(100, 0, 0));
  CHECK_FALSE(world.pointSolid({0, 0, 0}));
}

TEST_CASE("hull falls onto a floor and rests on its origin plane") {
  const auto world = makeWorld({boxBrush({-100, -10, -100}, {100, 0, 100})});
  // Player origin is at its feet (mins.y == 0), so it stops with origin.y ~ 0.
  const auto trace = world.traceHull({0, 10, 0}, {0, -10, 0}, kPlayer);
  CHECK(trace.fraction < 1.0F);
  CHECK(trace.endPos.y == doctest::Approx(0.0F).epsilon(kEps));
  CHECK(trace.endPos.y >= 0.0F);
  CHECK(trace.normal == vec3(0, 1, 0));
  CHECK(trace.brush == 0);
  CHECK_FALSE(trace.startSolid);
}

TEST_CASE("ray hits the near face of a wall") {
  const auto world = makeWorld({boxBrush({10, -50, -50}, {20, 50, 50})});
  const auto trace = world.traceRay({0, 0, 0}, {100, 0, 0});
  CHECK(trace.endPos.x == doctest::Approx(10.0F).epsilon(kEps));
  CHECK(trace.normal == vec3(-1, 0, 0));
}

TEST_CASE("a trace that stops short does not hit") {
  const auto world = makeWorld({boxBrush({10, -50, -50}, {20, 50, 50})});
  const auto trace = world.traceRay({0, 0, 0}, {5, 0, 0});
  CHECK(trace.fraction == 1.0F);
  CHECK(trace.endPos == vec3(5, 0, 0));
}

TEST_CASE("start and end inside solid") {
  const auto world = makeWorld({boxBrush({-50, -50, -50}, {50, 50, 50})});
  const auto trace = world.traceRay({0, 0, 0}, {10, 0, 0});
  CHECK(trace.startSolid);
  CHECK(trace.allSolid);
  CHECK(trace.fraction == 0.0F);
  CHECK(world.pointSolid({0, 0, 0}));
  CHECK_FALSE(world.pointSolid({60, 0, 0}));
}

TEST_CASE("starting inside solid but leaving it is startSolid, not allSolid") {
  const auto world = makeWorld({boxBrush({-50, -50, -50}, {50, 50, 50})});
  const auto trace = world.traceRay({0, 0, 0}, {100, 0, 0});
  CHECK(trace.startSolid);
  CHECK_FALSE(trace.allSolid);
}

TEST_CASE("hull passes a pillar edge it only touches in the expanded-plane approximation") {
  const auto world = makeWorld({diamond()});
  const Hull box{{-16, -16, -16}, {16, 16, 16}};
  // Hull spans x in [11, 43]; the diamond's vertex is at x = 10: no contact.
  CHECK(world.traceHull({27, 0, -100}, {27, 0, 100}, box).fraction == 1.0F);
  // Shifted to span x in [9, 41]: the vertex is hit when the hull reaches z = 0.
  const auto hit = world.traceHull({25, 0, -100}, {25, 0, 100}, box);
  CHECK(hit.fraction < 1.0F);
  CHECK(hit.endPos.z == doctest::Approx(-16.0F).epsilon(0.5));
}

TEST_CASE("sliding normal on a ramp is the ramp normal") {
  // Wedge: floor at y<=0, ramp rising along +x (45 degrees), bounded.
  const float s = std::sqrt(0.5F);
  BrushDesc wedge{{{{0, -1, 0}, 0}, {{-s, s, 0}, 0}, {{1, 0, 0}, 100}, {{0, 0, 1}, 50}, {{0, 0, -1}, 50}}};
  const auto world = makeWorld({wedge});
  const auto trace = world.traceHull({50, 100, 0}, {50, -100, 0}, kPlayer);
  REQUIRE(trace.fraction < 1.0F);
  CHECK(trace.normal.x == doctest::Approx(-s).epsilon(0.01));
  CHECK(trace.normal.y == doctest::Approx(s).epsilon(0.01));
}

TEST_CASE("an open brush is rejected") {
  BrushDesc open{{{{1, 0, 0}, 1}, {{0, 1, 0}, 1}, {{0, 0, 1}, 1}}};
  const std::vector<BrushDesc> brushes{open};
  auto world = CollisionWorld::create(brushes);
  REQUIRE_FALSE(world);
  CHECK(world.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("BVH agrees with testing every brush on its own") {
  std::uint32_t seed = 12345;
  const auto rnd = [&seed](float lo, float hi) {
    seed = seed * 1664525U + 1013904223U;
    return lo + (hi - lo) * static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
  };
  std::vector<BrushDesc> brushes;
  for (int i = 0; i < 150; ++i) {
    const vec3 c(rnd(-400, 400), rnd(-100, 100), rnd(-400, 400));
    const vec3 h(rnd(5, 40), rnd(5, 40), rnd(5, 40));
    brushes.push_back(boxBrush(c - h, c + h));
  }
  const auto world = makeWorld(brushes);
  std::vector<CollisionWorld> singles;
  for (const BrushDesc &b : brushes) {
    singles.push_back(makeWorld({b}));
  }
  for (int i = 0; i < 300; ++i) {
    const vec3 a(rnd(-500, 500), rnd(-150, 150), rnd(-500, 500));
    const vec3 b(rnd(-500, 500), rnd(-150, 150), rnd(-500, 500));
    float best = 1.0F;
    bool startSolid = false;
    for (const CollisionWorld &single : singles) {
      const auto t = single.traceHull(a, b, kPlayer);
      best = std::min(best, t.fraction);
      startSolid = startSolid || t.startSolid;
    }
    const auto t = world.traceHull(a, b, kPlayer);
    CHECK(t.fraction == doctest::Approx(best));
    CHECK(t.startSolid == startSolid);
  }
}

// Run with: game_tests --no-skip -tc="collision benchmark"
TEST_CASE("collision benchmark" * doctest::skip()) {
  std::uint32_t seed = 99;
  const auto rnd = [&seed](float lo, float hi) {
    seed = seed * 1664525U + 1013904223U;
    return lo + (hi - lo) * static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
  };
  std::vector<BrushDesc> brushes;
  for (int i = 0; i < 2000; ++i) {
    const vec3 c(rnd(-2000, 2000), rnd(-200, 200), rnd(-2000, 2000));
    const vec3 h(rnd(5, 60), rnd(5, 60), rnd(5, 60));
    brushes.push_back(boxBrush(c - h, c + h));
  }
  const auto world = makeWorld(brushes);
  constexpr int kTraces = 200000;
  float sink = 0.0F;
  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < kTraces; ++i) {
    const vec3 a(rnd(-2000, 2000), rnd(-200, 200), rnd(-2000, 2000));
    // Typical movement trace: one tick of travel (~4 units at 250 u/s, 128 Hz).
    const auto t = world.traceHull(a, a + vec3(rnd(-4, 4), rnd(-4, 4), rnd(-4, 4)), kPlayer);
    sink += t.fraction;
  }
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - begin).count();
  MESSAGE("2000 brushes, short trace: ", ns / kTraces, " ns/trace (sink ", sink, ")");
}
