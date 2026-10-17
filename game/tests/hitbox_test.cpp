#include <doctest/doctest.h>

#include "game/lag_compensation.h"

#include <cmath>
#include <numbers>

using namespace Paper;
using glm::vec3;

namespace {
HitboxSet setFor(const PlayerPose &pose) {
  HitboxSet set;
  FixedHitboxProvider().hitboxes(pose, set);
  return set;
}
// Shoots horizontally along -Z from `z` at height `y`.
std::optional<HitboxHit> shootAt(const HitboxSet &set, float y, float x = 0.0F, float z = 500.0F) {
  return rayVsHitboxes(set, {x, y, z}, {0, 0, -1}, 2000.0F);
}
} // namespace

TEST_CASE("hit groups are found at the right heights") {
  const auto set = setFor({});
  CHECK(shootAt(set, 67)->group == HitGroup::Head);
  CHECK(shootAt(set, 52)->group == HitGroup::Chest);
  CHECK(shootAt(set, 37)->group == HitGroup::Stomach);
  CHECK(shootAt(set, 15, 4.0F)->group == HitGroup::RightLeg);
  CHECK(shootAt(set, 15, -4.0F)->group == HitGroup::LeftLeg);
  CHECK(shootAt(set, 50, 12.0F)->group == HitGroup::RightArm);
  CHECK_FALSE(shootAt(set, 80).has_value());
  CHECK_FALSE(shootAt(set, 50, 40.0F).has_value());
}

TEST_CASE("the hit distance and point are exact") {
  const auto hit = shootAt(setFor({}), 67);
  REQUIRE(hit.has_value());
  CHECK(hit->distance == doctest::Approx(500.0F - 4.5F));
  CHECK(hit->point.z == doctest::Approx(4.5F));
}

TEST_CASE("hitboxes rotate with the player's yaw") {
  PlayerPose pose;
  pose.yaw = std::numbers::pi_v<float> / 2.0F;
  const auto set = setFor(pose);
  // Local (12, 50, 0), the right arm, maps to world (0, 50, -12) at yaw 90 degrees.
  const auto hit = rayVsHitboxes(set, {0, 50, -500}, {0, 0, 1}, 2000.0F);
  REQUIRE(hit.has_value());
  CHECK(hit->group == HitGroup::RightArm);
  // The chest is now seen from the side, so a shot at x = 12 along -Z misses.
  CHECK_FALSE(shootAt(set, 50, 12.0F).has_value());
}

TEST_CASE("a shot from behind or above works") {
  const auto set = setFor({});
  const auto back = rayVsHitboxes(set, {0, 52, -500}, {0, 0, 1}, 2000.0F);
  REQUIRE(back.has_value());
  CHECK(back->group == HitGroup::Chest);
  const auto top = rayVsHitboxes(set, {0, 500, 0}, {0, -1, 0}, 2000.0F);
  REQUIRE(top.has_value());
  CHECK(top->group == HitGroup::Head);
}

TEST_CASE("max distance limits hits") {
  const auto set = setFor({});
  CHECK_FALSE(rayVsHitboxes(set, {0, 67, 500}, {0, 0, -1}, 100.0F).has_value());
}

TEST_CASE("crouching lowers the head") {
  PlayerPose pose;
  pose.ducked = true;
  const auto set = setFor(pose);
  CHECK(shootAt(set, 67).has_value() == false);
  CHECK(shootAt(set, 50)->group == HitGroup::Head);
}

TEST_CASE("the provider is swappable behind the interface") {
  struct OneBox final : HitboxProvider {
    void hitboxes(const PlayerPose &pose, HitboxSet &out) const override {
      out.origin = pose.origin;
      out.yaw = pose.yaw;
      out.boxes[0] = {HitGroup::Chest, {-10, 0, -10}, {10, 70, 10}};
      out.count = 1;
    }
  };
  const HitboxProvider &provider = OneBox();
  HitboxSet set;
  provider.hitboxes({}, set);
  CHECK(shootAt(set, 67)->group == HitGroup::Chest);
}

TEST_CASE("pose history interpolates between ticks") {
  LagCompensation lag;
  for (std::uint32_t t = 100; t < 110; ++t) {
    PlayerPose p;
    p.origin = {static_cast<float>(t - 100) * 10.0F, 0, 0};
    lag.record(t, 3, p);
  }
  CHECK(lag.poseAt(3, 104.0)->origin.x == doctest::Approx(40.0F));
  CHECK(lag.poseAt(3, 104.5)->origin.x == doctest::Approx(45.0F));
  CHECK(lag.poseAt(3, 108.25)->origin.x == doctest::Approx(82.5F));
  CHECK_FALSE(lag.poseAt(3, 50.0).has_value());
  CHECK_FALSE(lag.poseAt(4, 104.0).has_value());
  lag.clear(3);
  CHECK_FALSE(lag.poseAt(3, 104.0).has_value());
}

TEST_CASE("teleports are not interpolated") {
  LagCompensation lag;
  PlayerPose a;
  PlayerPose b;
  b.origin = {1000, 0, 0};
  lag.record(10, 0, a);
  lag.record(11, 0, b);
  CHECK(lag.poseAt(0, 10.4)->origin.x == 0.0F);
  CHECK(lag.poseAt(0, 10.6)->origin.x == 1000.0F);
}

TEST_CASE("rewinding is clamped") {
  CHECK(LagCompensation::clampViewTick(500.0, 400, 26) == 400.0);
  CHECK(LagCompensation::clampViewTick(100.0, 400, 26) == 374.0);
  CHECK(LagCompensation::clampViewTick(390.5, 400, 26) == 390.5);
  CHECK(LagCompensation::clampViewTick(-5.0, 3, 26) == 0.0);
}

TEST_CASE("a shooter hits where the target was, not where it is") {
  // The target runs along +X at 10 units per tick. The shooter saw it 10
  // ticks ago and aims at that old position.
  LagCompensation lag;
  for (std::uint32_t t = 0; t <= 40; ++t) {
    PlayerPose p;
    p.origin = {static_cast<float>(t) * 10.0F, 0, 0};
    lag.record(t, 1, p);
  }
  const FixedHitboxProvider provider;
  const auto shoot = [&](double viewTick) {
    const auto pose = lag.poseAt(1, LagCompensation::clampViewTick(viewTick, 40, 26));
    HitboxSet set;
    provider.hitboxes(*pose, set);
    // Aimed at x = 300 (where the target was at tick 30).
    return rayVsHitboxes(set, {300, 52, 500}, {0, 0, -1}, 2000.0F).has_value();
  };
  CHECK(shoot(30.0));
  CHECK_FALSE(shoot(40.0));
}
