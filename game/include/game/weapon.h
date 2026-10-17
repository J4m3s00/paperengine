#pragma once

#include "game/movement.h"
#include "game/weapon_def.h"

#include <array>
#include <cstdint>

// The shared weapon simulation: fire rate, ammo, reload, switching, recoil
// and spread. Runs identically on the client (prediction) and the server, one
// step per usercmd. Time is the usercmd sequence number in fixed point (1/256
// tick) so both sides agree exactly. Hits are resolved by the server only.
namespace Paper {
inline constexpr std::int64_t kSubTicksPerTick = 256;

struct WeaponAmmo {
  std::int32_t clip = 0;
  std::int32_t reserve = 0;
  bool operator==(const WeaponAmmo &) const = default;
};

struct CombatState {
  std::uint8_t weapon = 0;
  std::array<WeaponAmmo, kMaxWeapons> ammo{};
  // Absolute times in sub-ticks of the usercmd sequence clock. 0 = none.
  std::int64_t nextAttack = 0;
  std::int64_t reloadEnd = 0;
  std::int64_t drawEnd = 0;
  std::int64_t lastShot = -1000000;
  std::uint16_t shotIndex = 0;
  bool attackHeld = false;
  // Extra spread from recent shots, degrees.
  float extraSpread = 0.0F;
  // View punch in degrees: x = pitch (up), y = yaw (left).
  glm::vec2 punch{0.0F};
  // Where the punch is heading: the pattern position of the last shot. The
  // punch follows it smoothly instead of jumping.
  glm::vec2 punchTarget{0.0F};

  [[nodiscard]] bool reloading() const { return reloadEnd != 0; }
};

// Server-owned health; replicated to everyone.
struct Vitals {
  std::uint8_t health = 100;
  std::uint8_t armor = 100;
  bool alive = true;
  bool operator==(const Vitals &) const = default;
};

// A full loadout with every weapon's starting ammo, the first weapon drawn.
[[nodiscard]] CombatState makeLoadout(const WeaponDb &db, std::uint8_t firstWeapon = 0);

// What a step produced. The shot (if any) is for the server to resolve and
// for the client to show.
struct ShotInfo {
  glm::vec3 origin{0.0F};
  glm::vec3 direction{0.0F, 0.0F, -1.0F};
  std::uint8_t weapon = 0;
  float spreadDegrees = 0.0F;
};
struct WeaponEvents {
  bool fired = false;
  bool dryFired = false;
  bool reloadStarted = false;
  bool reloadFinished = false;
  bool switched = false;
  ShotInfo shot;
};

// Speed limit of the current weapon for movement.
[[nodiscard]] inline float weaponMaxSpeed(const CombatState &state, const WeaponDb &db) {
  return db.at(state.weapon).maxSpeed;
}

// Advances `state` by the usercmd with sequence `seq`. `move` is the player's
// movement state after this tick's movement; `eye` its eye position.
WeaponEvents stepWeapon(CombatState &state, const UserCmd &cmd, const PlayerState &move,
                        const glm::vec3 &eye, std::uint32_t seq, const WeaponDb &db);

// Unit view direction for angles in radians (yaw 0 looks along -Z).
[[nodiscard]] glm::vec3 viewDirection(float yaw, float pitch);

// Damage of one hit before armor. `distance` in units.
[[nodiscard]] float hitDamage(const WeaponDef &weapon, bool head, float groupMultiplier,
                              float distance);
struct ArmorResult {
  float health = 0.0F;
  float armorLost = 0.0F;
};
// CS armor: a share of the damage (1 - armorRatio/2) is absorbed at half
// cost to the armor, never more than the armor left.
[[nodiscard]] ArmorResult applyArmor(const WeaponDef &weapon, float damage, float armor);
} // namespace Paper
