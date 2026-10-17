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

namespace {
void checkSameMap(const MapData &a, const MapData &b) {
  REQUIRE(a.brushes.size() == b.brushes.size());
  for (std::size_t i = 0; i < a.brushes.size(); ++i) {
    CAPTURE(i);
    CHECK(a.brushes[i].kind == b.brushes[i].kind);
    CHECK(a.brushes[i].material == b.brushes[i].material);
    REQUIRE(a.brushes[i].brush.planes.size() == b.brushes[i].brush.planes.size());
    for (std::size_t p = 0; p < a.brushes[i].brush.planes.size(); ++p) {
      const Plane &pa = a.brushes[i].brush.planes[p];
      const Plane &pb = b.brushes[i].brush.planes[p];
      CHECK(glm::distance(pa.normal, pb.normal) < 1e-5F);
      CHECK(pa.dist == doctest::Approx(pb.dist).epsilon(1e-5));
    }
  }
  REQUIRE(a.entities.size() == b.entities.size());
  for (std::size_t i = 0; i < a.entities.size(); ++i) {
    CHECK(a.entities[i].type == b.entities[i].type);
    CHECK(glm::distance(a.entities[i].origin, b.entities[i].origin) < 1e-5F);
    CHECK(a.entities[i].yaw == doctest::Approx(b.entities[i].yaw).epsilon(1e-5));
  }
}
} // namespace

TEST_CASE("brushes remember how they were authored") {
  const auto map = parseMap(R"({"version":1,"brushes":[
    {"box":[0,0,0,10,20,30],"material":"wall"},
    {"ramp":{"min":[0,0],"max":[100,50],"low":0,"high":40,"rising":"+x"}},
    {"planes":[[1,0,0,5],[-1,0,0,5],[0,1,0,5],[0,-1,0,5],[0,0,1,5],[0,0,-1,5]]}]})");
  REQUIRE(map.has_value());
  CHECK(map->brushes[0].kind == BrushKind::Box);
  CHECK(map->brushes[0].maxs == glm::vec3(10, 20, 30));
  CHECK(map->brushes[1].kind == BrushKind::Ramp);
  CHECK(map->brushes[1].ramp.high == 40.0F);
  CHECK(map->brushes[1].ramp.bottom == -64.0F);
  CHECK(map->brushes[2].kind == BrushKind::Planes);
}

TEST_CASE("serializing a map and loading it again gives the same map") {
  const auto original = parseMap(R"({"version":1,"brushes":[
    {"box":[-5,0,-5,10,20,30],"material":"wall"},
    {"ramp":{"min":[0,0],"max":[100,50],"low":0,"high":40,"bottom":-10,"rising":"-z"},"material":"ramp"},
    {"planes":[[1,0,0,5],[-1,0,0,5],[0,1,0,5],[0,-1,0,5],[0,0,1,5],[0,0,-1,5]]}],
    "entities":[{"type":"spawn","origin":[1,2,3],"yaw":90},{"type":"bot","origin":[4,5,6]}]})");
  REQUIRE(original.has_value());
  const auto again = parseMap(serializeMap(*original));
  REQUIRE(again.has_value());
  checkSameMap(*original, *again);
  CHECK(again->entities[0].yaw == doctest::Approx(glm::half_pi<float>()));
}

TEST_CASE("the dev world and the arena survive a save and load") {
  for (const char *name : {"dev_world.json", "arena.json"}) {
    CAPTURE(name);
    const auto original = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / name);
    REQUIRE(original.has_value());
    const auto again = parseMap(serializeMap(*original));
    REQUIRE(again.has_value());
    checkSameMap(*original, *again);
    // Identical collision: a trace through both worlds agrees.
    const auto wa = original->buildCollision();
    const auto wb = again->buildCollision();
    REQUIRE(wa.has_value());
    REQUIRE(wb.has_value());
    for (float x = -500.0F; x <= 500.0F; x += 50.0F) {
      const auto ta = wa->traceHull({x, 300, 700}, {x, 20, -700}, kStandingHull);
      const auto tb = wb->traceHull({x, 300, 700}, {x, 20, -700}, kStandingHull);
      CHECK(ta.fraction == tb.fraction);
      CHECK(ta.normal == tb.normal);
    }
  }
}

TEST_CASE("saveMap writes a file loadMap reads back") {
  MapData map;
  map.brushes.push_back(MapBrush::box({0, 0, 0}, {64, 64, 64}, "box"));
  map.entities.push_back({"spawn", {0, 70, 0}, 0.5F});
  const auto path = std::filesystem::temp_directory_path() / "paper_save_map_test.json";
  REQUIRE(saveMap(map, path).has_value());
  const auto loaded = loadMap(path);
  std::filesystem::remove(path);
  REQUIRE(loaded.has_value());
  checkSameMap(map, *loaded);
  CHECK_FALSE(saveMap(map, "/nonexistent-dir/x/map.json").has_value());
}

TEST_CASE("degenerate authored shapes are rejected") {
  CHECK_FALSE(parseMap(R"({"version":1,"brushes":[{"box":[0,0,0,0,10,10]}]})").has_value());
  RampDef bad;
  bad.min = {10, 10};
  bad.max = {0, 0};
  CHECK_FALSE(rampPlanes(bad).has_value());
  MapBrush box = MapBrush::box({0, 0, 0}, {1, 1, 1});
  box.maxs.x = -1.0F;
  CHECK_FALSE(box.rebuild().has_value());
}

TEST_CASE("the map checksum follows the collision, not the bookkeeping") {
  const auto a = parseMap(R"({"version":1,"brushes":[{"box":[0,0,0,10,10,10],"material":"wall"}],
    "entities":[{"type":"spawn","origin":[0,20,0]}]})");
  const auto sameShape = parseMap(R"({"version":1,"brushes":[{"box":[0,0,0,10,10,10],"material":"floor"}]})");
  const auto bigger = parseMap(R"({"version":1,"brushes":[{"box":[0,0,0,10,10,11]}]})");
  REQUIRE(a.has_value());
  REQUIRE(sameShape.has_value());
  REQUIRE(bigger.has_value());
  CHECK(mapChecksum(*a) == mapChecksum(*sameShape)); // material and entities do not matter
  CHECK(mapChecksum(*a) != mapChecksum(*bigger));
  // And a saved-then-loaded map keeps its checksum.
  const auto again = parseMap(serializeMap(*a));
  REQUIRE(again.has_value());
  CHECK(mapChecksum(*a) == mapChecksum(*again));
  for (const char *name : {"dev_world.json", "arena.json"}) {
    const auto m = loadMap(std::filesystem::path(PAPER_MAPS_DIR) / name);
    REQUIRE(m.has_value());
    CHECK(mapChecksum(*m) == mapChecksum(*parseMap(serializeMap(*m))));
  }
}
