#include "game/weapon.h"

#include "game/tick.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Paper {
namespace {
constexpr double kServerTickRateD = kServerTickRate;

std::int64_t toSub(double seconds) {
  return static_cast<std::int64_t>(std::llround(seconds * kServerTickRateD * kSubTicksPerTick));
}

std::uint64_t mix(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

float unit(std::uint64_t bits) { return static_cast<float>(bits >> 40) * (1.0F / 16777216.0F); }

float baseInaccuracy(const WeaponDef &w, const PlayerState &move) {
  const InaccuracyDef &i = w.inaccuracy;
  if (!move.onGround) {
    return i.air;
  }
  const float base = move.ducked ? i.crouch : i.stand;
  const float speed = std::hypot(move.velocity.x, move.velocity.z);
  // Walking slowly (shift) stays accurate; full running is fully inaccurate.
  const float t = std::clamp((speed / w.maxSpeed - 0.34F) / 0.66F, 0.0F, 1.0F);
  return base + (i.move - base) * t;
}
} // namespace

CombatState makeLoadout(const WeaponDb &db, std::uint8_t firstWeapon) {
  CombatState s;
  s.weapon = firstWeapon;
  for (std::size_t i = 0; i < db.size(); ++i) {
    s.ammo[i] = {db.at(i).magazine, db.at(i).reserve};
  }
  return s;
}

glm::vec3 viewDirection(float yaw, float pitch) {
  const float cp = std::cos(pitch);
  return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

WeaponEvents stepWeapon(CombatState &s, const UserCmd &cmd, const PlayerState &move,
                        const glm::vec3 &eye, std::uint32_t seq, const WeaponDb &db) {
  WeaponEvents ev;
  const std::int64_t now = static_cast<std::int64_t>(seq) * kSubTicksPerTick;
  constexpr float dt = 1.0F / static_cast<float>(kServerTickRateD);

  // Switch weapons.
  if (cmd.weaponSelect > 0 && cmd.weaponSelect - 1 < static_cast<int>(db.size()) &&
      cmd.weaponSelect - 1 != s.weapon) {
    s.weapon = static_cast<std::uint8_t>(cmd.weaponSelect - 1);
    s.reloadEnd = 0;
    s.drawEnd = now + toSub(db.at(s.weapon).drawTime);
    s.shotIndex = 0;
    s.punch = glm::vec2(0.0F);
    s.punchTarget = glm::vec2(0.0F);
    ev.switched = true;
  }
  const WeaponDef &w = db.at(s.weapon);
  WeaponAmmo &ammo = s.ammo[s.weapon];

  // Recoil and spread settle while not shooting.
  s.extraSpread = std::max(0.0F, s.extraSpread - w.inaccuracy.recovery * dt);
  if (now - s.lastShot > toSub(w.cycleTime + w.recoil.resetTime)) {
    s.shotIndex = 0;
  }
  if (now - s.lastShot > toSub(w.cycleTime)) {
    const float len = glm::length(s.punchTarget);
    const float step = w.recoil.recovery * dt;
    s.punchTarget = len <= step ? glm::vec2(0.0F) : s.punchTarget * ((len - step) / len);
  }
  // The visible punch eases toward its target (exponential, frame-rate free).
  s.punch += (s.punchTarget - s.punch) * (1.0F - std::exp(-dt / w.recoil.smoothing));

  if (s.reloadEnd != 0 && now >= s.reloadEnd) {
    const std::int32_t take = std::min(w.magazine - ammo.clip, ammo.reserve);
    ammo.clip += take;
    ammo.reserve -= take;
    s.reloadEnd = 0;
    ev.reloadFinished = true;
  }

  const auto startReload = [&] {
    if (!w.melee && !s.reloading() && ammo.clip < w.magazine && ammo.reserve > 0 &&
        now >= s.drawEnd) {
      s.reloadEnd = now + toSub(w.reloadTime);
      ev.reloadStarted = true;
    }
  };
  if (cmd.reload) {
    startReload();
  }

  const bool wantFire = cmd.attack && (w.automatic || !s.attackHeld);
  s.attackHeld = cmd.attack;
  if (wantFire && !s.reloading() && now >= s.drawEnd && now >= s.nextAttack) {
    if (!w.melee && ammo.clip <= 0) {
      ev.dryFired = true;
      s.nextAttack = now + toSub(w.cycleTime);
      startReload();
    } else {
      // Keep the exact rhythm while holding the trigger; after a pause start fresh.
      const std::int64_t base = s.nextAttack >= now - kSubTicksPerTick ? s.nextAttack : now;
      s.nextAttack = base + toSub(w.cycleTime);
      if (!w.melee) {
        --ammo.clip;
      }
      float spread = 0.0F;
      float pitch = cmd.pitch;
      float yaw = cmd.yaw;
      if (!w.melee) {
        const auto &pattern = w.recoil.pattern;
        // This bullet leaves where the view is now; the kick it causes builds
        // up over the next few ticks.
        pitch += glm::radians(s.punch.x);
        yaw += glm::radians(s.punch.y);
        s.punchTarget = pattern[std::min<std::size_t>(s.shotIndex, pattern.size() - 1)];
        ++s.shotIndex;
        spread = baseInaccuracy(w, move) + s.extraSpread;
        s.extraSpread = std::min(s.extraSpread + w.inaccuracy.perShot, w.inaccuracy.maxExtra);
      }
      s.lastShot = now;

      glm::vec3 dir = viewDirection(yaw, pitch);
      if (spread > 0.0F) {
        const glm::vec3 right(std::cos(yaw), 0.0F, -std::sin(yaw));
        const glm::vec3 up = glm::cross(right, dir);
        const std::uint64_t r = mix(static_cast<std::uint64_t>(seq) * 8 + s.weapon);
        const float radius = std::tan(glm::radians(spread)) * std::sqrt(unit(r));
        const float angle = 2.0F * std::numbers::pi_v<float> * unit(mix(r));
        dir = glm::normalize(dir + right * (radius * std::cos(angle)) + up * (radius * std::sin(angle)));
      }
      ev.fired = true;
      ev.shot = {eye, dir, s.weapon, spread};
    }
  }
  return ev;
}

float hitDamage(const WeaponDef &weapon, bool head, float groupMultiplier, float distance) {
  const float falloff = std::pow(weapon.rangeModifier, std::max(distance, 0.0F) / 500.0F);
  return weapon.damage * falloff * (head ? weapon.headshotMultiplier : groupMultiplier);
}

ArmorResult applyArmor(const WeaponDef &weapon, float damage, float armor) {
  constexpr float kArmorBonus = 0.5F;
  if (armor <= 0.0F) {
    return {damage, 0.0F};
  }
  float health = damage * weapon.armorRatio * 0.5F;
  float armorLost = (damage - health) * kArmorBonus;
  if (armorLost > armor) {
    armorLost = armor;
    health = damage - armor / kArmorBonus;
  }
  return {health, armorLost};
}
} // namespace Paper
