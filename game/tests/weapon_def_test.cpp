#include <doctest/doctest.h>

#include "game/weapon_def.h"

using namespace Paper;

namespace {
WeaponDb loadSlice() {
  auto db = WeaponDb::loadDirectory(PAPER_WEAPONS_DIR);
  REQUIRE(db.has_value());
  return std::move(*db);
}

const char *kMinimal = R"({"id":"x","order":1,"slot":"primary","damage":10,"cycleTime":0.1,
  "magazine":5,"reloadTime":1.0,"recoil":{"pattern":[[0,0]]}})";
} // namespace

TEST_CASE("the slice weapons load, ordered by their order field") {
  const WeaponDb db = loadSlice();
  REQUIRE(db.size() == 7);
  const char *expected[] = {"ak47", "m4a4", "awp", "glock18", "usp_s", "deagle", "knife"};
  for (std::size_t i = 0; i < 7; ++i) {
    CHECK(db.at(i).id == expected[i]);
  }
  CHECK(db.indexOf("awp") == 2);
  CHECK(db.indexOf("nope") == -1);
}

TEST_CASE("slice weapon values are sane and match the CS:GO reference") {
  const WeaponDb db = loadSlice();
  const WeaponDef &ak = db.at(static_cast<std::size_t>(db.indexOf("ak47")));
  CHECK(ak.damage == 36.0F);
  CHECK(ak.automatic);
  CHECK(ak.maxSpeed == 215.0F);
  CHECK(ak.magazine == 30);
  CHECK(ak.recoil.pattern.size() == 30);
  const WeaponDef &awp = db.at(static_cast<std::size_t>(db.indexOf("awp")));
  CHECK(awp.damage * awp.headshotMultiplier > 100.0F);
  CHECK_FALSE(awp.automatic);
  CHECK(db.at(static_cast<std::size_t>(db.indexOf("knife"))).melee);
  for (const WeaponDef &w : db.all()) {
    CHECK(w.cycleTime > 0.0F);
    CHECK(w.maxSpeed >= 200.0F);
    CHECK(w.maxSpeed <= 250.0F);
    if (!w.melee) {
      CHECK(w.magazine > 0);
      CHECK_FALSE(w.recoil.pattern.empty());
    }
  }
}

TEST_CASE("recoil patterns climb like a spray pattern should") {
  const WeaponDb db = loadSlice();
  const auto &p = db.at(0).recoil.pattern;
  for (std::size_t i = 1; i < 12; ++i) {
    CHECK(p[i].x >= p[i - 1].x); // pitch only goes up early in the spray
  }
  CHECK(p.back().x > 10.0F);
}

TEST_CASE("invalid weapons are rejected with a reason") {
  CHECK(WeaponDb::parse({kMinimal}).has_value());
  CHECK_FALSE(WeaponDb::parse({"not json"}).has_value());
  CHECK_FALSE(WeaponDb::parse({R"({"id":"x","order":1,"slot":"primary"})"}).has_value());
  CHECK_FALSE(WeaponDb::parse({R"({"id":"x","order":1,"slot":"bad","damage":1,"cycleTime":1})"}).has_value());
  CHECK_FALSE(WeaponDb::parse({R"({"id":"x","order":1,"slot":"primary","damage":1,"cycleTime":0})"}).has_value());
  // A gun without a recoil pattern.
  CHECK_FALSE(WeaponDb::parse({R"({"id":"x","order":1,"slot":"primary","damage":1,"cycleTime":1,
    "magazine":5,"reloadTime":1})"}).has_value());
  // Duplicate order and duplicate id.
  const std::string second = std::string(kMinimal);
  CHECK_FALSE(WeaponDb::parse({kMinimal, second}).has_value());
}

TEST_CASE("adding a weapon is just another file") {
  std::string other = kMinimal;
  other.replace(other.find("\"x\""), 3, "\"y\"");
  other.replace(other.find("\"order\":1"), 9, "\"order\":2");
  const auto db = WeaponDb::parse({kMinimal, other});
  REQUIRE(db.has_value());
  CHECK(db->size() == 2);
  CHECK(db->indexOf("y") == 1);
}
