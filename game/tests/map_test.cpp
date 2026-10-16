#include <doctest/doctest.h>

#include "game/brush_geometry.h"
#include "game/map.h"
#include "game/movement.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

using namespace Paper;
using glm::vec3;

namespace {
float faceArea(const BrushFace &f) {
  vec3 sum(0.0F);
  for (std::size_t i = 0; i < f.vertices.size(); ++i) {
    sum += glm::cross(f.vertices[i], f.vertices[(i + 1) % f.vertices.size()]);
  }
  return 0.5F * glm::dot(sum, f.plane.normal); // positive when CCW seen from outside
}
} // namespace

TEST_CASE("box brush has six outward counter-clockwise faces") {
  const auto faces = brushFaces(boxBrush({0, 0, 0}, {10, 20, 30}));
  REQUIRE(faces.size() == 6);
  float total = 0.0F;
  for (const BrushFace &f : faces) {
    CHECK(f.vertices.size() == 4);
    CHECK(faceArea(f) > 0.0F);
    total += faceArea(f);
  }
  CHECK(total == doctest::Approx(2.0F * (10 * 20 + 10 * 30 + 20 * 30)));
}

TEST_CASE("redundant planes add no faces") {
  BrushDesc b = boxBrush({0, 0, 0}, {10, 10, 10});
  b.planes.push_back({{1, 0, 0}, 50}); // far outside, clips nothing, has no area
  CHECK(brushFaces(b).size() == 6);
}

TEST_CASE("parse boxes, ramps and entities") {
  const auto map = parseMap(R"({
    "version": 1,
    "brushes": [
      { "box": [-100, -10, -100, 100, 0, 100], "material": "floor" },
      { "ramp": { "min": [0, -50], "max": [200, 50], "low": 0, "high": 100,
                  "rising": "+x", "bottom": -10 } }
    ],
    "entities": [ { "type": "spawn", "origin": [0, 2, 0], "yaw": 90 },
                  { "type": "bot", "origin": [5, 0, 5] } ]
  })");
  REQUIRE(map.has_value());
  CHECK(map->brushes.size() == 2);
  CHECK(map->brushes[0].material == "floor");
  REQUIRE(map->entitiesOfType("spawn").size() == 1);
  CHECK(map->entitiesOfType("spawn")[0].yaw == doctest::Approx(glm::half_pi<float>()));
  CHECK(map->entitiesOfType("bot").size() == 1);
}

TEST_CASE("a ramp rises along its direction") {
  const auto map = parseMap(R"({ "version": 1, "brushes": [
    { "ramp": { "min": [0, -50], "max": [200, 50], "low": 0, "high": 100,
                "rising": "+x", "bottom": -10 } } ] })");
  REQUIRE(map);
  const auto world = map->buildCollision();
  REQUIRE(world);
  const auto mid = world->traceRay({100, 300, 0}, {100, -300, 0});
  CHECK(mid.endPos.y == doctest::Approx(50.0F).epsilon(0.01));
  CHECK(mid.normal.y > 0.0F);
  CHECK(mid.normal.x < 0.0F); // tilts back toward the low end
  const auto high = world->traceRay({190, 300, 0}, {190, -300, 0});
  CHECK(high.endPos.y == doctest::Approx(95.0F).epsilon(0.01));
}

TEST_CASE("ramps rise in all four directions") {
  for (const char *dir : {"+x", "-x", "+z", "-z"}) {
    const std::string json =
        std::string(R"({ "version": 1, "brushes": [ { "ramp": { "min": [0, 0], "max": [100, 100],
        "low": 0, "high": 50, "rising": ")") + dir + R"(", "bottom": -10 } } ] })";
    const auto map = parseMap(json);
    REQUIRE(map);
    const auto world = map->buildCollision();
    REQUIRE(world);
    // High end is at +x/+z for "+x"/"+z" and at the min side for "-x"/"-z".
    const bool positive = dir[0] == '+';
    const bool alongX = dir[1] == 'x';
    const vec3 highPoint = alongX ? vec3(positive ? 90.0F : 10.0F, 300, 50)
                                  : vec3(50, 300, positive ? 90.0F : 10.0F);
    const auto hit = world->traceRay(highPoint, highPoint + vec3(0, -600, 0));
    CHECK(hit.endPos.y == doctest::Approx(45.0F).epsilon(0.02));
  }
}

TEST_CASE("plane brushes are accepted") {
  const auto map = parseMap(R"({ "version": 1, "brushes": [ { "planes":
    [[1,0,0,5],[-1,0,0,5],[0,1,0,5],[0,-1,0,5],[0,0,1,5],[0,0,-1,5]] } ] })");
  REQUIRE(map);
  const auto world = map->buildCollision();
  REQUIRE(world);
  CHECK(world->pointSolid({0, 0, 0}));
}

TEST_CASE("bad maps report where the problem is") {
  CHECK_FALSE(parseMap("{ not json"));
  CHECK_FALSE(parseMap(R"({ "brushes": [] })")); // no version
  auto unknown = parseMap(R"({ "version": 1, "brushes": [ { "sphere": 1 } ] })");
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().message.find("brush 0") != std::string::npos);
  CHECK_FALSE(parseMap(R"({ "version": 1, "brushes": [ { "box": [0,0,0,1] } ] })"));
  CHECK_FALSE(parseMap(R"({ "version": 1, "brushes": [ { "box": [0,0,0,0,1,1] } ] })"));
  auto badEntity = parseMap(R"({ "version": 1, "entities": [ { "type": "spawn" } ] })");
  REQUIRE_FALSE(badEntity);
  CHECK(badEntity.error().message.find("entity 0") != std::string::npos);
}

TEST_CASE("missing map file is an Io error") {
  auto map = loadMap("/nonexistent/map.json");
  REQUIRE_FALSE(map);
  CHECK(map.error().code == ErrorCode::Io);
}

#ifdef PAPER_MAPS_DIR
TEST_CASE("the dev world map loads and has a spawn above ground") {
  auto map = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / "dev_world.json");
  REQUIRE(map);
  auto world = map->buildCollision();
  REQUIRE(world);
  const auto spawns = map->entitiesOfType("spawn");
  REQUIRE_FALSE(spawns.empty());
  for (const MapEntity &s : spawns) {
    CHECK_FALSE(world->pointSolid(s.origin));
    const auto down = world->traceHull(s.origin, s.origin - vec3(0, 50, 0), kStandingHull);
    CHECK(down.fraction < 1.0F);
  }
}
#endif
