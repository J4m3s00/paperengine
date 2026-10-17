#include "game/weapon_def.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Paper {
namespace {
using json = nlohmann::json;

Error invalid(const std::string &m) { return {ErrorCode::InvalidArgument, m}; }

template <typename T> T get(const json &j, const char *key, T fallback) {
  const auto it = j.find(key);
  return it == j.end() ? fallback : it->get<T>();
}

std::expected<WeaponDef, Error> parseWeapon(const std::string &text) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return std::unexpected(invalid("weapon file is not valid JSON"));
  }
  WeaponDef w;
  try {
    w.id = j.at("id").get<std::string>();
    w.displayName = get<std::string>(j, "displayName", w.id);
    w.order = j.at("order").get<int>();
    const auto slot = j.at("slot").get<std::string>();
    if (slot == "primary") w.slot = WeaponSlot::Primary;
    else if (slot == "secondary") w.slot = WeaponSlot::Secondary;
    else if (slot == "melee") w.slot = WeaponSlot::Melee;
    else return std::unexpected(invalid(w.id + ": unknown slot '" + slot + "'"));
    w.automatic = get(j, "automatic", false);
    w.melee = get(j, "melee", false);
    w.damage = j.at("damage").get<float>();
    w.armorRatio = get(j, "armorRatio", 1.0F);
    w.headshotMultiplier = get(j, "headshotMultiplier", 4.0F);
    w.range = get(j, "range", 8192.0F);
    w.rangeModifier = get(j, "rangeModifier", 1.0F);
    w.cycleTime = j.at("cycleTime").get<float>();
    w.maxSpeed = get(j, "maxSpeed", 250.0F);
    w.magazine = get(j, "magazine", 0);
    w.reserve = get(j, "reserve", 0);
    w.reloadTime = get(j, "reloadTime", 0.0F);
    w.drawTime = get(j, "drawTime", 0.5F);
    if (const auto it = j.find("inaccuracy"); it != j.end()) {
      InaccuracyDef &i = w.inaccuracy;
      i.stand = get(*it, "stand", 0.0F);
      i.crouch = get(*it, "crouch", 0.0F);
      i.move = get(*it, "move", 0.0F);
      i.air = get(*it, "air", 0.0F);
      i.perShot = get(*it, "perShot", 0.0F);
      i.maxExtra = get(*it, "maxExtra", 0.0F);
      i.recovery = get(*it, "recovery", 1.0F);
    }
    if (const auto it = j.find("recoil"); it != j.end()) {
      for (const auto &p : it->at("pattern")) {
        w.recoil.pattern.emplace_back(p.at(0).get<float>(), p.at(1).get<float>());
      }
      w.recoil.resetTime = get(*it, "resetTime", 0.4F);
      w.recoil.recovery = get(*it, "recovery", 30.0F);
      w.recoil.smoothing = get(*it, "smoothing", 0.04F);
    }
  } catch (const json::exception &e) {
    return std::unexpected(invalid(std::string("weapon file: ") + e.what()));
  }
  if (w.recoil.smoothing <= 0.0F) {
    return std::unexpected(invalid(w.id + ": recoil smoothing must be positive"));
  }
  if (w.id.empty() || w.cycleTime <= 0.0F || w.damage <= 0.0F || w.maxSpeed <= 0.0F ||
      w.rangeModifier <= 0.0F || w.range <= 0.0F) {
    return std::unexpected(invalid(w.id + ": id, damage, cycleTime, maxSpeed and range must be positive"));
  }
  if (!w.melee && (w.magazine <= 0 || w.reloadTime <= 0.0F || w.recoil.pattern.empty())) {
    return std::unexpected(invalid(w.id + ": a gun needs magazine, reloadTime and a recoil pattern"));
  }
  return w;
}
} // namespace

std::expected<WeaponDb, Error> WeaponDb::parse(const std::vector<std::string> &jsonFiles) {
  WeaponDb db;
  for (const std::string &text : jsonFiles) {
    auto weapon = parseWeapon(text);
    if (!weapon) {
      return std::unexpected(weapon.error());
    }
    db.m_weapons.push_back(std::move(*weapon));
  }
  std::sort(db.m_weapons.begin(), db.m_weapons.end(),
            [](const WeaponDef &a, const WeaponDef &b) { return a.order < b.order; });
  if (db.m_weapons.size() > kMaxWeapons) {
    return std::unexpected(invalid("too many weapons (max " + std::to_string(kMaxWeapons) + ")"));
  }
  for (std::size_t i = 1; i < db.m_weapons.size(); ++i) {
    if (db.m_weapons[i].order == db.m_weapons[i - 1].order) {
      return std::unexpected(invalid("weapons '" + db.m_weapons[i - 1].id + "' and '" +
                                     db.m_weapons[i].id + "' share order " +
                                     std::to_string(db.m_weapons[i].order)));
    }
  }
  for (std::size_t i = 0; i < db.m_weapons.size(); ++i) {
    for (std::size_t k = i + 1; k < db.m_weapons.size(); ++k) {
      if (db.m_weapons[i].id == db.m_weapons[k].id) {
        return std::unexpected(invalid("duplicate weapon id '" + db.m_weapons[i].id + "'"));
      }
    }
  }
  return db;
}

std::expected<WeaponDb, Error> WeaponDb::loadDirectory(const std::filesystem::path &dir) {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  if (ec) {
    return std::unexpected(Error{ErrorCode::Io, "cannot read weapons directory " + dir.string()});
  }
  std::sort(files.begin(), files.end());
  std::vector<std::string> texts;
  for (const auto &path : files) {
    std::ifstream in(path);
    if (!in) {
      return std::unexpected(Error{ErrorCode::Io, "cannot open " + path.string()});
    }
    std::stringstream ss;
    ss << in.rdbuf();
    texts.push_back(ss.str());
  }
  auto db = parse(texts);
  if (!db) {
    return std::unexpected(Error{db.error().code, dir.string() + ": " + db.error().message});
  }
  return db;
}

int WeaponDb::indexOf(std::string_view id) const {
  for (std::size_t i = 0; i < m_weapons.size(); ++i) {
    if (m_weapons[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
} // namespace Paper
