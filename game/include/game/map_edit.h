#pragma once

#include "game/map.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The editing model behind the map editor: add, move, resize and delete
// brushes and entities with undo/redo, ray picking and validation. No UI and
// no rendering, so it is testable headless.
namespace Paper {
struct Selection {
  enum class Type { None, Brush, Entity, Prop };
  Type type = Type::None;
  int index = -1;

  [[nodiscard]] bool valid() const { return type != Type::None && index >= 0; }
  bool operator==(const Selection &) const = default;
};

struct PickHit {
  Selection what;
  float distance = 0.0F;
};

struct MapIssue {
  std::string text;
  bool error = false; // errors stop the map from loading, warnings do not
};

// Entity types the game knows about.
inline constexpr const char *kEntityTypes[] = {"spawn", "spawn_t", "spawn_ct", "bot"};

// Meters per Source unit: the environment kit is modeled in meters.
inline constexpr float kUnitsPerMeter = 39.3701F;

// The bounds of a prop model in its own space (meters), if it is known.
using PropExtentsFn =
    std::function<std::optional<std::pair<glm::vec3, glm::vec3>>(const std::string &model)>;

class MapEditor {
public:
  explicit MapEditor(MapData map = {});

  [[nodiscard]] const MapData &map() const { return m_map; }
  // Replaces the whole map (a fresh start: no undo history, nothing selected).
  void load(MapData map);
  // Tells the editor how big prop models are (it has no renderer).
  void setPropExtents(PropExtentsFn fn) { m_propExtents = std::move(fn); }
  [[nodiscard]] bool dirty() const { return m_dirty; }
  void markSaved() { m_dirty = false; }
  // Changes with every edit, undo, redo and load (cheap "did the map change?").
  [[nodiscard]] std::uint64_t revision() const { return m_revision; }

  [[nodiscard]] Selection selection() const { return m_selection; }
  void select(Selection s);

  // Edits made between begin and end form one undo step (dragging).
  void beginGroup();
  void endGroup();

  int addBox(const glm::vec3 &mins, const glm::vec3 &maxs, std::string material = "box");
  int addRamp(const RampDef &ramp, std::string material = "ramp");
  int addEntity(std::string type, const glm::vec3 &origin, float yaw = 0.0F);
  int addProp(std::string model, const glm::vec3 &origin, float yaw = 0.0F, float scale = 1.0F);
  void setProp(int prop, std::string model, const glm::vec3 &origin, float yaw, float scale);
  // Adds a box brush (material "collision") around the prop, so players
  // bump into it. Returns the brush index, or -1 if the size is unknown.
  int addPropCollision(int prop);
  void remove(Selection s);
  // Copies the selected brush or entity, shifted by `offset`, and selects the copy.
  Selection duplicate(Selection s, const glm::vec3 &offset);
  // Moves a brush or entity. A plane brush shifts all its planes.
  void move(Selection s, const glm::vec3 &delta);
  [[nodiscard]] std::expected<void, Error> setBox(int brush, const glm::vec3 &mins,
                                                  const glm::vec3 &maxs);
  [[nodiscard]] std::expected<void, Error> setRamp(int brush, const RampDef &ramp);
  void setMaterial(int brush, std::string material);
  void setEntity(int entity, std::string type, const glm::vec3 &origin, float yaw);

  [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
  [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
  bool undo();
  bool redo();

  // The nearest brush or entity hit by the ray (direction normalized).
  [[nodiscard]] std::optional<PickHit> pick(const glm::vec3 &origin, const glm::vec3 &dir,
                                            float maxDistance) const;
  // Axis-aligned bounds of a brush or the box around an entity.
  [[nodiscard]] std::optional<std::pair<glm::vec3, glm::vec3>> bounds(Selection s) const;
  [[nodiscard]] std::vector<MapIssue> validate() const;

private:
  struct Snapshot {
    MapData map;
    Selection selection;
  };
  void record();
  void touch() {
    m_dirty = true;
    ++m_revision;
  }
  [[nodiscard]] bool exists(Selection s) const;

  MapData m_map;
  PropExtentsFn m_propExtents;
  Selection m_selection;
  std::vector<Snapshot> m_undo;
  std::vector<Snapshot> m_redo;
  int m_groupDepth = 0;
  bool m_groupRecorded = false;
  bool m_dirty = false;
  std::uint64_t m_revision = 0;
};
} // namespace Paper
