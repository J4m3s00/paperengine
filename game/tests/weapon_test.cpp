#include <doctest/doctest.h>

#include "game/weapon.h"

#include <cmath>

using namespace Paper;

namespace {
const WeaponDb &db() {
  static const WeaponDb instance = [] {
    auto d = WeaponDb::loadDirectory(PAPER_WEAPONS_DIR);
    REQUIRE(d.has_value());
    return std::move(*d);
  }();
  return instance;
}
constexpr std::uint8_t kAk = 0;
constexpr std::uint8_t kAwp = 2;
constexpr std::uint8_t kGlock = 3;
constexpr std::uint8_t kKnife = 6;

struct Shooter {
  CombatState state = makeLoadout(db(), kAk);
  PlayerState move;
  std::uint32_t seq = 1000; // far from the start so nothing is "recently fired"
  int shots = 0;
  WeaponEvents last;

  Shooter() { move.onGround = true; }

  WeaponEvents tick(UserCmd cmd) {
    cmd.pitch = 0.0F;
    last = stepWeapon(state, cmd, move, {0, 64, 0}, seq++, db());
    shots += last.fired ? 1 : 0;
    return last;
  }
  void idle(int ticks) {
    for (int i = 0; i < ticks; ++i) tick({});
  }
  void hold(int ticks, UserCmd cmd = {}) {
    cmd.attack = true;
    for (int i = 0; i < ticks; ++i) tick(cmd);
  }
};
} // namespace

TEST_CASE("an automatic rifle fires at its cycle time") {
  Shooter s;
  s.hold(128); // one second
  // 0.1 s cycle: shot at 0, 0.1, ... 0.9 -> 10 shots in the first second.
  CHECK(s.shots == 10);
  CHECK(s.state.ammo[kAk].clip == 20);
}

TEST_CASE("fire rate does not drift over a long spray") {
  Shooter s;
  s.state.ammo[kAk].clip = 1000;
  s.hold(128 * 10);
  CHECK(s.shots == 100);
}

TEST_CASE("a semi-automatic pistol needs the trigger released") {
  Shooter s;
  s.state = makeLoadout(db(), kGlock);
  s.hold(128);
  CHECK(s.shots == 1);
  s.idle(2);
  UserCmd c;
  c.attack = true;
  s.tick(c);
  CHECK(s.shots == 2);
}

TEST_CASE("the cycle time limits tapping too") {
  Shooter s;
  s.state = makeLoadout(db(), kGlock);
  UserCmd press;
  press.attack = true;
  s.tick(press);
  s.tick({});
  s.tick(press); // 2 ticks later: still cooling down (0.15 s = 19 ticks)
  CHECK(s.shots == 1);
}

TEST_CASE("an empty magazine dry fires and reloads from the reserve") {
  Shooter s;
  s.state.ammo[kAk] = {1, 90};
  s.hold(1);
  CHECK(s.shots == 1);
  CHECK(s.state.ammo[kAk].clip == 0);
  s.hold(20);
  CHECK(s.shots == 1);
  CHECK(s.state.reloading());
  s.idle(static_cast<int>(2.43 * 128) + 2);
  CHECK_FALSE(s.state.reloading());
  CHECK(s.state.ammo[kAk].clip == 30);
  CHECK(s.state.ammo[kAk].reserve == 60);
}

TEST_CASE("a manual reload tops up and keeps the rest in reserve") {
  Shooter s;
  s.state.ammo[kAk] = {10, 12};
  UserCmd reload;
  reload.reload = true;
  const auto ev = s.tick(reload);
  CHECK(ev.reloadStarted);
  s.idle(static_cast<int>(2.43 * 128) + 2);
  CHECK(s.state.ammo[kAk].clip == 22); // only 12 rounds were left to load
  CHECK(s.state.ammo[kAk].reserve == 0);
}

TEST_CASE("reloading with a full magazine or no reserve does nothing") {
  Shooter s;
  UserCmd reload;
  reload.reload = true;
  CHECK_FALSE(s.tick(reload).reloadStarted);
  s.state.ammo[kAk] = {5, 0};
  CHECK_FALSE(s.tick(reload).reloadStarted);
}

TEST_CASE("you cannot shoot while reloading, switching cancels the reload") {
  Shooter s;
  s.state.ammo[kAk] = {10, 50};
  UserCmd reload;
  reload.reload = true;
  s.tick(reload);
  s.hold(30);
  CHECK(s.shots == 0);
  UserCmd sw;
  sw.weaponSelect = kGlock + 1;
  CHECK(s.tick(sw).switched);
  CHECK_FALSE(s.state.reloading());
  CHECK(s.state.weapon == kGlock);
}

TEST_CASE("a freshly drawn weapon cannot fire until drawn") {
  Shooter s;
  UserCmd sw;
  sw.weaponSelect = kGlock + 1;
  s.tick(sw);
  s.hold(static_cast<int>(0.8 * 128) - 4);
  CHECK(s.shots == 0);
  s.idle(8);
  s.hold(1);
  CHECK(s.shots == 1);
}

TEST_CASE("the spray pattern follows the data and resets after a pause") {
  Shooter s;
  const auto &pattern = db().at(kAk).recoil.pattern;
  for (std::size_t shot = 0; shot < 12; ++shot) {
    // Wait until the next shot, then check the punch it produced.
    while (!s.tick([] { UserCmd c; c.attack = true; return c; }()).fired) {}
    CHECK(s.state.punch.x == doctest::Approx(pattern[shot].x));
    CHECK(s.state.punch.y == doctest::Approx(pattern[shot].y));
  }
  s.idle(128); // one second pause
  CHECK(s.state.shotIndex == 0);
  CHECK(s.state.punch.x == doctest::Approx(0.0F).epsilon(0.01));
}

TEST_CASE("the punch recovers gradually when you stop") {
  Shooter s;
  s.hold(40);
  const float before = s.state.punch.x;
  REQUIRE(before > 5.0F);
  s.idle(16);
  CHECK(s.state.punch.x < before);
  CHECK(s.state.punch.x > 0.0F);
}

TEST_CASE("the spray goes past the pattern's end on the last entry") {
  Shooter s;
  s.state.ammo[kAk].clip = 100;
  s.hold(128 * 5);
  const auto &pattern = db().at(kAk).recoil.pattern;
  CHECK(s.state.punch.x == doctest::Approx(pattern.back().x));
}

TEST_CASE("inaccuracy: standing < running < airborne, crouching is best") {
  const auto spreadFor = [](bool ground, bool ducked, float speed) {
    Shooter s;
    s.move.onGround = ground;
    s.move.ducked = ducked;
    s.move.velocity = {0, 0, -speed};
    UserCmd c;
    c.attack = true;
    return s.tick(c).shot.spreadDegrees;
  };
  const float stand = spreadFor(true, false, 0);
  const float crouch = spreadFor(true, true, 0);
  const float walk = spreadFor(true, false, 215 * 0.3F);
  const float run = spreadFor(true, false, 215);
  const float air = spreadFor(false, false, 0);
  CHECK(crouch < stand);
  CHECK(walk == doctest::Approx(stand));
  CHECK(stand < run);
  CHECK(run < air);
  CHECK(stand < 0.5F);
}

TEST_CASE("each shot adds spread, up to a cap, and it recovers") {
  Shooter s;
  s.state.ammo[kAk].clip = 1000;
  float prev = -1.0F;
  for (int shot = 0; shot < 4; ++shot) {
    while (!s.tick([] { UserCmd c; c.attack = true; return c; }()).fired) {}
    CHECK(s.last.shot.spreadDegrees > prev);
    prev = s.last.shot.spreadDegrees;
  }
  s.hold(128 * 4);
  CHECK(s.last.shot.spreadDegrees <= db().at(kAk).inaccuracy.stand + db().at(kAk).inaccuracy.maxExtra + 1e-4F);
  s.idle(128 * 2);
  UserCmd c;
  c.attack = true;
  s.state.shotIndex = 0;
  s.tick(c);
  CHECK(s.last.shot.spreadDegrees == doctest::Approx(db().at(kAk).inaccuracy.stand));
}

TEST_CASE("spread is deterministic per usercmd and stays inside the cone") {
  const auto shotAt = [](std::uint32_t seq) {
    CombatState state = makeLoadout(db(), kAk);
    PlayerState move;
    move.onGround = true;
    move.velocity = {0, 0, -215};
    UserCmd c;
    c.attack = true;
    c.yaw = 0.3F;
    return stepWeapon(state, c, move, {0, 64, 0}, seq, db()).shot;
  };
  CHECK(shotAt(500).direction == shotAt(500).direction);
  CHECK(shotAt(500).direction != shotAt(501).direction);
  int differing = 0;
  for (std::uint32_t seq = 100; seq < 300; ++seq) {
    const ShotInfo shot = shotAt(seq);
    const glm::vec3 center = viewDirection(0.3F, 0.0F);
    const float angle = glm::degrees(std::acos(std::min(1.0F, glm::dot(center, shot.direction))));
    CHECK(angle <= shot.spreadDegrees + 1e-3F);
    CHECK(glm::length(shot.direction) == doctest::Approx(1.0F));
    differing += angle > shot.spreadDegrees * 0.5F ? 1 : 0;
  }
  CHECK(differing > 20); // actually spreads over the cone (area-uniform)
}

TEST_CASE("the first standing shot is nearly dead accurate") {
  Shooter s;
  UserCmd c;
  c.attack = true;
  const auto ev = s.tick(c);
  const glm::vec3 center = viewDirection(0.0F, 0.0F);
  CHECK(glm::degrees(std::acos(std::min(1.0F, glm::dot(center, ev.shot.direction)))) < 0.31F);
}

TEST_CASE("the knife needs no ammo and does not recoil") {
  Shooter s;
  s.state = makeLoadout(db(), kKnife);
  s.hold(128);
  CHECK(s.shots >= 2);
  CHECK(s.state.punch == glm::vec2(0.0F));
  CHECK(s.state.ammo[kKnife].clip == 0);
}

TEST_CASE("the weapon limits the movement speed") {
  Shooter s;
  CHECK(weaponMaxSpeed(s.state, db()) == 215.0F);
  s.state.weapon = kKnife;
  CHECK(weaponMaxSpeed(s.state, db()) == 250.0F);
}

TEST_CASE("damage: headshots, range falloff and hit groups") {
  const WeaponDef &ak = db().at(kAk);
  CHECK(hitDamage(ak, false, 1.0F, 0.0F) == doctest::Approx(36.0F));
  CHECK(hitDamage(ak, true, 1.0F, 0.0F) == doctest::Approx(144.0F));
  CHECK(hitDamage(ak, false, 0.75F, 0.0F) == doctest::Approx(27.0F));
  CHECK(hitDamage(ak, false, 1.0F, 1000.0F) == doctest::Approx(36.0F * 0.98F * 0.98F));
  const WeaponDef &awp = db().at(kAwp);
  CHECK(hitDamage(awp, false, 1.0F, 0.0F) >= 100.0F);
}

TEST_CASE("armor absorbs part of the damage") {
  const WeaponDef &ak = db().at(kAk);
  const ArmorResult r = applyArmor(ak, 36.0F, 100.0F);
  CHECK(r.health == doctest::Approx(36.0F * 1.55F * 0.5F));
  CHECK(r.armorLost == doctest::Approx((36.0F - r.health) * 0.5F));
  CHECK(applyArmor(ak, 36.0F, 0.0F).health == 36.0F);
  // Almost no armor left: all it has is used, the rest hurts.
  const ArmorResult low = applyArmor(ak, 36.0F, 2.0F);
  CHECK(low.armorLost == 2.0F);
  CHECK(low.health == doctest::Approx(36.0F - 4.0F));
  // Pistols (ratio 1.0) lose more to armor than rifles.
  CHECK(applyArmor(db().at(kGlock), 30.0F, 100.0F).health < applyArmor(ak, 30.0F, 100.0F).health);
}

TEST_CASE("a replayed weapon stream gives identical state") {
  const auto run = [] {
    Shooter s;
    for (int t = 0; t < 2000; ++t) {
      UserCmd c;
      c.attack = (t / 40) % 2 == 0;
      c.reload = t % 500 == 499;
      c.weaponSelect = t % 700 == 699 ? static_cast<std::uint8_t>(1 + (t / 700) % 6) : 0;
      c.yaw = 0.001F * static_cast<float>(t);
      s.tick(c);
    }
    return s.state;
  };
  const CombatState a = run();
  const CombatState b = run();
  CHECK(a.punch == b.punch);
  CHECK(a.nextAttack == b.nextAttack);
  CHECK(a.ammo == b.ammo);
  CHECK(a.extraSpread == b.extraSpread);
}
