#pragma once

#include "base/error.h"

#include <glm/glm.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Data-driven weapons: one JSON file per weapon in `weapons/`. Adding a
// weapon means adding a file, unless it needs behaviour the simulation does
// not have yet.
namespace Paper {
enum class WeaponSlot { Primary, Secondary, Melee };

// Spread cone half-angles in degrees.
struct InaccuracyDef {
  float stand = 0.0F;
  float crouch = 0.0F;
  float move = 0.0F;
  float air = 0.0F;
  // Added to the extra spread by every shot, capped, decaying per second.
  float perShot = 0.0F;
  float maxExtra = 0.0F;
  float recovery = 1.0F;
};

struct RecoilDef {
  // Cumulative view punch (pitch up, yaw left; degrees) after shot i. Shots
  // beyond the end repeat the last entry.
  std::vector<glm::vec2> pattern;
  // Seconds without shooting (beyond the cycle time) before the pattern
  // restarts at shot 0.
  float resetTime = 0.4F;
  // Degrees per second the punch returns to zero while not shooting.
  float recovery = 30.0F;
  // Time constant (seconds) of the view kick following each shot's pattern
  // position: smaller is snappier.
  float smoothing = 0.04F;
};

struct WeaponDef {
  std::string id;
  std::string displayName;
  // Unique and stable: defines the weapon's index and number key.
  int order = 0;
  WeaponSlot slot = WeaponSlot::Primary;
  bool automatic = false;
  bool melee = false;

  float damage = 0.0F;
  // CS "armor penetration" value (halved when applied; 2.0 ignores armor).
  float armorRatio = 1.0F;
  float headshotMultiplier = 4.0F;
  float range = 8192.0F;
  // Damage is multiplied by rangeModifier^(distance / 500).
  float rangeModifier = 1.0F;
  float cycleTime = 0.1F;
  float maxSpeed = 250.0F;
  int magazine = 0;
  int reserve = 0;
  float reloadTime = 0.0F;
  float drawTime = 0.5F;

  InaccuracyDef inaccuracy;
  RecoilDef recoil;
};

// All weapons, sorted by `order`. The index of a weapon is its position here
// and is what the network protocol carries.
class WeaponDb {
public:
  [[nodiscard]] static std::expected<WeaponDb, Error> loadDirectory(const std::filesystem::path &dir);
  [[nodiscard]] static std::expected<WeaponDb, Error> parse(const std::vector<std::string> &jsonFiles);

  [[nodiscard]] std::size_t size() const { return m_weapons.size(); }
  [[nodiscard]] const WeaponDef &at(std::size_t index) const { return m_weapons[index]; }
  // Index of the weapon with this id, or -1.
  [[nodiscard]] int indexOf(std::string_view id) const;
  [[nodiscard]] const std::vector<WeaponDef> &all() const { return m_weapons; }

private:
  std::vector<WeaponDef> m_weapons;
};

inline constexpr std::size_t kMaxWeapons = 8;
} // namespace Paper
