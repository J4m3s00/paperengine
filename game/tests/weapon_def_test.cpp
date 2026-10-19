#include <doctest/doctest.h>

#include "game/weapon_def.h"

#include <filesystem>
#include <fstream>
#include <sstream>

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

TEST_CASE("weapons can name a 3D model and muzzle for the client") {
  const WeaponDb db = loadSlice();
  const WeaponDef &ak = db.at(static_cast<std::size_t>(db.indexOf("ak47")));
  CHECK(ak.model == "ak47");
  CHECK(ak.muzzle.x > 0.3F);
  CHECK(db.at(static_cast<std::size_t>(db.indexOf("knife"))).model.empty());
  const auto parsed = WeaponDb::parse({R"({"id":"x","order":1,"slot":"primary","damage":10,"cycleTime":0.1,
    "magazine":5,"reloadTime":1.0,"recoil":{"pattern":[[0,0]]},"model":"m","muzzle":[1,2,3]})"});
  REQUIRE(parsed.has_value());
  CHECK(parsed->at(0).model == "m");
  CHECK(parsed->at(0).muzzle == glm::vec3(1, 2, 3));
}

TEST_CASE("weapon grips default to identity and are read and saved as data") {
  const auto parsed = WeaponDb::parse({kMinimal, R"({"id":"y","order":2,"slot":"primary","damage":10,
    "cycleTime":0.1,"magazine":5,"reloadTime":1.0,"recoil":{"pattern":[[0,0]]},
    "gripPosition":[0.1,0.2,0.3],"gripRotation":[0,90,-45]})"});
  REQUIRE(parsed.has_value());
  CHECK(parsed->at(0).gripPosition == glm::vec3(0.0F));
  CHECK(parsed->at(0).gripRotation == glm::vec3(0.0F));
  CHECK(parsed->at(1).gripPosition == glm::vec3(0.1F, 0.2F, 0.3F));
  CHECK(parsed->at(1).gripRotation == glm::vec3(0.0F, 90.0F, -45.0F));

  const auto file = std::filesystem::temp_directory_path() / "paper_grip_test.json";
  {
    std::ofstream(file) << kMinimal;
  }
  REQUIRE(saveWeaponGrip(file, {1, 2, 3}, {10, 20, 30}).has_value());
  std::ifstream in(file);
  std::stringstream text;
  text << in.rdbuf();
  const auto reloaded = WeaponDb::parse({text.str()});
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->at(0).id == "x");
  CHECK(reloaded->at(0).magazine == 5);
  CHECK(reloaded->at(0).gripPosition == glm::vec3(1, 2, 3));
  CHECK(reloaded->at(0).gripRotation == glm::vec3(10, 20, 30));
  std::filesystem::remove(file);
}
