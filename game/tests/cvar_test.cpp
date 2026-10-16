#include <doctest/doctest.h>

#include "base/cvar.h"

#include <filesystem>
#include <fstream>

using namespace Paper;

TEST_CASE("cvars start at their default and can be reset") {
  CvarRegistry reg;
  auto& accel = reg.add<float>("sv_accelerate", 5.5f, 0.0f, 100.0f);
  CHECK(accel.value() == 5.5f);
  accel.set(7.0f);
  CHECK(accel.value() == 7.0f);
  reg.resetAll();
  CHECK(accel.value() == 5.5f);
}

TEST_CASE("set clamps to the range") {
  CvarRegistry reg;
  auto& friction = reg.add<float>("sv_friction", 5.2f, 0.0f, 20.0f);
  REQUIRE(reg.set("sv_friction", "99"));
  CHECK(friction.value() == 20.0f);
  REQUIRE(reg.set("sv_friction", "-3"));
  CHECK(friction.value() == 0.0f);
}

TEST_CASE("parsing: ints, floats and bools") {
  CvarRegistry reg;
  auto& i = reg.add<int>("i", 1, 0, 10);
  auto& f = reg.add<float>("f", 1.0f, -1.0f, 1.0f);
  auto& b = reg.addBool("b", false);
  CHECK(reg.set("i", "7"));
  CHECK(i.value() == 7);
  CHECK(reg.set("f", "0.25"));
  CHECK(f.value() == 0.25f);
  CHECK(reg.set("b", "true"));
  CHECK(b.value());
  CHECK(reg.set("b", "0"));
  CHECK_FALSE(b.value());
}

TEST_CASE("invalid values are rejected and leave the value unchanged") {
  CvarRegistry reg;
  auto& i = reg.add<int>("i", 3, 0, 10);
  auto result = reg.set("i", "3.5");
  REQUIRE_FALSE(result);
  CHECK(result.error().code == ErrorCode::InvalidArgument);
  CHECK_FALSE(reg.set("i", "abc"));
  CHECK_FALSE(reg.set("i", ""));
  CHECK(i.value() == 3);
}

TEST_CASE("unknown cvar is NotFound") {
  CvarRegistry reg;
  auto result = reg.set("nope", "1");
  REQUIRE_FALSE(result);
  CHECK(result.error().code == ErrorCode::NotFound);
}

TEST_CASE("execute handles comments, blanks, whitespace") {
  CvarRegistry reg;
  auto& i = reg.add<int>("i", 0, 0, 10);
  CHECK(reg.execute(""));
  CHECK(reg.execute("   # comment"));
  CHECK(reg.execute("  i \t 4  \r"));
  CHECK(i.value() == 4);
  CHECK_FALSE(reg.execute("i"));
}

TEST_CASE("all() is sorted by name and find works") {
  CvarRegistry reg;
  reg.add<int>("b", 0, 0, 1);
  reg.add<int>("a", 0, 0, 1);
  reg.add<int>("c", 0, 0, 1);
  REQUIRE(reg.all().size() == 3);
  CHECK(reg.all()[0]->name() == "a");
  CHECK(reg.all()[2]->name() == "c");
  CHECK(reg.find("b") != nullptr);
  CHECK(reg.find("z") == nullptr);
}

TEST_CASE("loadFile applies valid lines and skips bad ones") {
  CvarRegistry reg;
  auto& i = reg.add<int>("i", 0, 0, 10);
  auto& j = reg.add<int>("j", 0, 0, 10);
  const auto path = (std::filesystem::temp_directory_path() / "paper_cvar_test.cfg").string();
  {
    std::ofstream out(path);
    out << "i 5\nbogus 1\nj 6\n";
  }
  CHECK(reg.loadFile(path));
  CHECK(i.value() == 5);
  CHECK(j.value() == 6);
  std::filesystem::remove(path);
  auto missing = reg.loadFile(path);
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == ErrorCode::Io);
}
