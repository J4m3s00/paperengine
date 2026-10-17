#include <doctest/doctest.h>

#include "game/map_edit.h"

#include <glm/gtc/constants.hpp>

using namespace Paper;
using glm::vec3;

namespace {
MapEditor floorEditor() {
  MapData map;
  map.brushes.push_back(MapBrush::box({-500, -64, -500}, {500, 0, 500}, "floor"));
  map.entities.push_back({"spawn", {0, 2, 0}, 0.0F});
  return MapEditor(map);
}
} // namespace

TEST_CASE("adding things selects them and marks the map dirty") {
  MapEditor ed;
  CHECK_FALSE(ed.dirty());
  const int b = ed.addBox({0, 0, 0}, {64, 64, 64}, "box");
  CHECK(b == 0);
  CHECK(ed.selection() == Selection{Selection::Type::Brush, 0});
  CHECK(ed.dirty());
  const int e = ed.addEntity("bot", {10, 0, 10}, 1.0F);
  CHECK(ed.selection() == Selection{Selection::Type::Entity, e});
  CHECK(ed.map().entities[0].type == "bot");
  CHECK(ed.map().brushes[0].brush.planes.size() == 6);
  const int r = ed.addRamp({{0, 0}, {100, 50}, 0, 32, -8, "+x"}, "ramp");
  CHECK(ed.map().brushes[static_cast<std::size_t>(r)].kind == BrushKind::Ramp);
}

TEST_CASE("move shifts boxes, ramps, plane brushes and entities") {
  MapEditor ed;
  ed.addBox({0, 0, 0}, {10, 10, 10});
  ed.addRamp({{0, 0}, {100, 50}, 0, 32, -8, "+x"});
  MapBrush planes = MapBrush::box({0, 0, 0}, {10, 10, 10});
  planes.kind = BrushKind::Planes;
  ed.addEntity("spawn", {0, 0, 0});
  ed.move({Selection::Type::Brush, 0}, {5, 6, 7});
  CHECK(ed.map().brushes[0].mins == vec3(5, 6, 7));
  CHECK(ed.map().brushes[0].maxs == vec3(15, 16, 17));
  // The solid follows: the +x plane of the box moved to x = 15.
  bool found = false;
  for (const Plane &p : ed.map().brushes[0].brush.planes) {
    found = found || (p.normal == vec3(1, 0, 0) && p.dist == doctest::Approx(15.0F));
  }
  CHECK(found);
  ed.move({Selection::Type::Brush, 1}, {10, 20, 30});
  CHECK(ed.map().brushes[1].ramp.min == glm::vec2(10, 30));
  CHECK(ed.map().brushes[1].ramp.low == 20.0F);
  CHECK(ed.map().brushes[1].ramp.bottom == 12.0F);
  ed.move({Selection::Type::Entity, 0}, {1, 2, 3});
  CHECK(ed.map().entities[0].origin == vec3(1, 2, 3));
}

TEST_CASE("moving a plane brush keeps it the same shape") {
  MapData map;
  MapBrush b = MapBrush::box({0, 0, 0}, {10, 20, 30});
  b.kind = BrushKind::Planes;
  map.brushes.push_back(b);
  MapEditor ed(map);
  ed.move({Selection::Type::Brush, 0}, {100, 0, 0});
  const auto hit = ed.pick({105, 10, 100}, {0, 0, -1}, 1000.0F);
  REQUIRE(hit.has_value());
  CHECK(hit->distance == doctest::Approx(70.0F));
  CHECK_FALSE(ed.pick({5, 10, 100}, {0, 0, -1}, 1000.0F).has_value());
}

TEST_CASE("resizing validates the shape") {
  MapEditor ed;
  ed.addBox({0, 0, 0}, {10, 10, 10});
  CHECK(ed.setBox(0, {0, 0, 0}, {20, 30, 40}).has_value());
  CHECK(ed.map().brushes[0].maxs == vec3(20, 30, 40));
  const auto before = ed.map().brushes[0].maxs;
  CHECK_FALSE(ed.setBox(0, {0, 0, 0}, {-1, 5, 5}).has_value());
  CHECK(ed.map().brushes[0].maxs == before); // unchanged
  CHECK_FALSE(ed.setRamp(0, {}).has_value());  // not a ramp
  ed.addRamp({{0, 0}, {10, 10}, 0, 5, -8, "+x"});
  RampDef bad = ed.map().brushes[1].ramp;
  bad.bottom = 100.0F;
  CHECK_FALSE(ed.setRamp(1, bad).has_value());
  bad.bottom = -20.0F;
  bad.rising = "-z";
  CHECK(ed.setRamp(1, bad).has_value());
}

TEST_CASE("undo and redo walk through the edits") {
  MapEditor ed = floorEditor();
  ed.addBox({0, 0, 0}, {64, 64, 64});
  ed.move({Selection::Type::Brush, 1}, {100, 0, 0});
  REQUIRE(ed.map().brushes.size() == 2);
  CHECK(ed.undo());
  CHECK(ed.map().brushes[1].mins == vec3(0, 0, 0));
  CHECK(ed.undo());
  CHECK(ed.map().brushes.size() == 1);
  CHECK_FALSE(ed.undo());
  CHECK(ed.redo());
  CHECK(ed.redo());
  CHECK(ed.map().brushes[1].mins == vec3(100, 0, 0));
  CHECK_FALSE(ed.redo());
  // A new edit forgets the redo history.
  ed.undo();
  ed.addEntity("bot", {0, 0, 0});
  CHECK_FALSE(ed.canRedo());
}

TEST_CASE("a grouped drag is one undo step") {
  MapEditor ed = floorEditor();
  ed.addBox({0, 0, 0}, {64, 64, 64});
  ed.beginGroup();
  for (int i = 0; i < 10; ++i) {
    ed.move({Selection::Type::Brush, 1}, {8, 0, 0});
  }
  ed.endGroup();
  CHECK(ed.map().brushes[1].mins.x == 80.0F);
  ed.undo();
  CHECK(ed.map().brushes[1].mins.x == 0.0F);
  ed.redo();
  CHECK(ed.map().brushes[1].mins.x == 80.0F);
}

TEST_CASE("remove and duplicate") {
  MapEditor ed = floorEditor();
  const Selection box{Selection::Type::Brush, 0};
  const Selection copy = ed.duplicate(box, {100, 0, 0});
  CHECK(copy == Selection{Selection::Type::Brush, 1});
  CHECK(ed.selection() == copy);
  CHECK(ed.map().brushes[1].mins.x == -400.0F);
  CHECK(ed.map().brushes[0].mins.x == -500.0F); // the original stays
  ed.undo(); // duplicate and its shift are one step
  CHECK(ed.map().brushes.size() == 1);
  ed.remove({Selection::Type::Entity, 0});
  CHECK(ed.map().entities.empty());
  CHECK(ed.selection() == Selection{});
  ed.undo();
  CHECK(ed.map().entities.size() == 1);
  ed.remove({Selection::Type::Brush, 99}); // out of range: ignored
  CHECK(ed.map().brushes.size() == 1);
}

TEST_CASE("picking finds the nearest brush or entity along a ray") {
  MapEditor ed;
  ed.addBox({-50, 0, -50}, {50, 100, 50}); // 0
  ed.addBox({-50, 0, -250}, {50, 100, -150}); // 1, behind the first
  ed.addEntity("spawn", {0, 0, 200}); // 2: in front
  const auto hit = ed.pick({0, 20, 500}, {0, 0, -1}, 2000.0F);
  REQUIRE(hit.has_value());
  CHECK(hit->what == Selection{Selection::Type::Entity, 0});
  const auto hit2 = ed.pick({0, 50, 100}, {0, 0, -1}, 2000.0F);
  CHECK(hit2->what == Selection{Selection::Type::Brush, 0});
  CHECK(hit2->distance == doctest::Approx(50.0F));
  CHECK_FALSE(ed.pick({0, 50, 100}, {0, 0, -1}, 20.0F).has_value()); // out of reach
  CHECK_FALSE(ed.pick({500, 50, 100}, {0, 0, -1}, 2000.0F).has_value());
  // Inside a brush looking out: it is not hit.
  CHECK(ed.pick({0, 50, 0}, {0, 0, -1}, 2000.0F)->what == Selection{Selection::Type::Brush, 1});
  // Looking along a face does not hit.
  CHECK_FALSE(ed.pick({60, 50, 500}, {0, 0, -1}, 2000.0F).has_value());
}

TEST_CASE("bounds of brushes and entities") {
  MapEditor ed;
  ed.addBox({1, 2, 3}, {4, 5, 6});
  ed.addRamp({{0, 0}, {100, 50}, 0, 32, -8, "+x"});
  ed.addEntity("spawn", {10, 0, 10});
  const auto box = ed.bounds({Selection::Type::Brush, 0});
  CHECK(box->first == vec3(1, 2, 3));
  const auto ramp = ed.bounds({Selection::Type::Brush, 1});
  CHECK(ramp->second.y == 32.0F);
  CHECK(ed.bounds({Selection::Type::Entity, 0})->second.y == 72.0F);
  CHECK_FALSE(ed.bounds({}).has_value());
}

TEST_CASE("validation finds problems and plays nice with a good map") {
  MapEditor good = floorEditor();
  CHECK(good.validate().empty());

  MapEditor empty;
  const auto issues = empty.validate();
  REQUIRE(issues.size() == 2);
  for (const MapIssue &i : issues) CHECK_FALSE(i.error);

  MapEditor bad = floorEditor();
  bad.addEntity("spawn", {0, -30, 0});     // inside the floor
  bad.addEntity("spawn", {0, 2000, 900});   // nothing below (off the floor)
  bad.addEntity("flag", {0, 2, 0});        // unknown type
  bad.addEntity("spawn_t", {0, 2, 0});     // a team without the other
  const auto bi = bad.validate();
  CHECK(bi.size() == 4);
}

TEST_CASE("loading replaces everything") {
  MapEditor ed = floorEditor();
  ed.addBox({0, 0, 0}, {1, 1, 1});
  ed.load(MapData{});
  CHECK(ed.map().brushes.empty());
  CHECK_FALSE(ed.canUndo());
  CHECK_FALSE(ed.dirty());
  CHECK(ed.selection() == Selection{});
}

TEST_CASE("an edited map round-trips through the file format and plays the same") {
  MapEditor ed = floorEditor();
  ed.addBox({100, 0, 100}, {200, 64, 200}, "wall");
  ed.addRamp({{-200, -200}, {-100, -100}, 0, 40, -16, "+z"}, "ramp");
  ed.move({Selection::Type::Brush, 1}, {16, 0, 0});
  const auto loaded = parseMap(serializeMap(ed.map()));
  REQUIRE(loaded.has_value());
  const auto a = ed.map().buildCollision();
  const auto b = loaded->buildCollision();
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  for (float x = -300.0F; x <= 300.0F; x += 25.0F) {
    const auto ta = a->traceRay({x, 300, 150}, {x, -100, 150});
    const auto tb = b->traceRay({x, 300, 150}, {x, -100, 150});
    CHECK(ta.fraction == tb.fraction);
  }
}

namespace {
// A kit with one piece: a 4 x 2 x 1 m wall (model space bounds are offset on purpose).
MapEditor propEditor() {
  MapEditor ed;
  ed.setPropExtents([](const std::string &model) -> std::optional<std::pair<vec3, vec3>> {
    if (model == "wall") return std::pair{vec3(11, 0, -18), vec3(15, 2, -17)};
    return std::nullopt;
  });
  return ed;
}
} // namespace

TEST_CASE("props can be added, moved, picked and removed") {
  MapEditor ed = propEditor();
  const int p = ed.addProp("wall", {100, 0, 0}, 0.0F);
  CHECK(ed.selection() == Selection{Selection::Type::Prop, p});
  ed.move({Selection::Type::Prop, p}, {10, 0, 0});
  CHECK(ed.map().props[0].origin == vec3(110, 0, 0));
  // 4 x 2 x 1 m = 157 x 79 x 39 units, base centered on the origin.
  const auto b = ed.bounds({Selection::Type::Prop, p});
  REQUIRE(b.has_value());
  CHECK(b->second.x - b->first.x == doctest::Approx(4.0F * kUnitsPerMeter));
  CHECK(b->second.y == doctest::Approx(2.0F * kUnitsPerMeter));
  CHECK(b->first.x == doctest::Approx(110.0F - 2.0F * kUnitsPerMeter));
  const auto hit = ed.pick({110, 30, 500}, {0, 0, -1}, 2000.0F);
  REQUIRE(hit.has_value());
  CHECK(hit->what == Selection{Selection::Type::Prop, p});
  CHECK_FALSE(ed.pick({110, 300, 500}, {0, 0, -1}, 2000.0F).has_value());
  const Selection copy = ed.duplicate({Selection::Type::Prop, p}, {0, 0, 200});
  CHECK(copy == Selection{Selection::Type::Prop, 1});
  CHECK(ed.map().props[1].origin == vec3(110, 0, 200));
  ed.remove(copy);
  CHECK(ed.map().props.size() == 1);
  ed.undo();
  CHECK(ed.map().props.size() == 2);
}

TEST_CASE("turning a prop swaps its footprint, collision follows the box") {
  MapEditor ed = propEditor();
  const int p = ed.addProp("wall", {0, 0, 0}, glm::half_pi<float>());
  const auto b = ed.bounds({Selection::Type::Prop, p});
  CHECK(b->second.x - b->first.x == doctest::Approx(1.0F * kUnitsPerMeter).epsilon(1e-3));
  CHECK(b->second.z - b->first.z == doctest::Approx(4.0F * kUnitsPerMeter).epsilon(1e-3));
  const int brush = ed.addPropCollision(p);
  REQUIRE(brush >= 0);
  const MapBrush &box = ed.map().brushes[static_cast<std::size_t>(brush)];
  CHECK(box.material == kCollisionMaterial);
  CHECK(box.maxs.y == doctest::Approx(2.0F * kUnitsPerMeter));
  // Solid now: a ray along z hits the collision brush.
  const auto world = ed.map().buildCollision();
  REQUIRE(world.has_value());
  CHECK(world->traceRay({0, 20, 400}, {0, 20, -400}).fraction < 1.0F);
  CHECK(ed.addPropCollision(99) == -1);
}

TEST_CASE("props with unknown models are flagged and cannot get collision") {
  MapEditor ed = propEditor();
  ed.addProp("mystery", {0, 0, 0});
  CHECK(ed.addPropCollision(0) == -1);
  bool flagged = false;
  for (const MapIssue &i : ed.validate()) {
    flagged = flagged || i.text.find("unknown model") != std::string::npos;
  }
  CHECK(flagged);
  ed.setProp(0, "wall", {5, 0, 5}, 0.0F, 2.0F);
  CHECK(ed.map().props[0].model == "wall");
  CHECK(ed.map().props[0].scale == 2.0F);
  ed.setProp(0, "", {5, 0, 5}, 0.0F, 2.0F); // invalid: ignored
  CHECK(ed.map().props[0].model == "wall");
}
