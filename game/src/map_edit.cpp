#include "game/map_edit.h"

#include "game/brush_geometry.h"
#include "game/movement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Paper {
namespace {
constexpr std::size_t kMaxUndo = 200;
// Entities are picked as a sphere around the middle of a player-sized body.
constexpr float kEntityRadius = 18.0F;
constexpr float kEntityCenterHeight = 18.0F;

// Ray vs convex brush: the distance at which the ray enters it, if it does.
std::optional<float> rayVsBrush(const BrushDesc &brush, const glm::vec3 &o, const glm::vec3 &d,
                                float maxDistance) {
  float enter = 0.0F;
  float leave = maxDistance;
  for (const Plane &p : brush.planes) {
    const float denom = glm::dot(p.normal, d);
    const float dist = glm::dot(p.normal, o) - p.dist; // > 0: in front of the plane
    if (std::fabs(denom) < 1e-8F) {
      if (dist > 0.0F) {
        return std::nullopt;
      }
      continue;
    }
    const float t = -dist / denom;
    if (denom < 0.0F) {
      enter = std::max(enter, t);
    } else {
      leave = std::min(leave, t);
    }
    if (enter > leave) {
      return std::nullopt;
    }
  }
  // Starting inside a brush does not count as hitting it.
  return enter > 0.0F ? std::optional<float>(enter) : std::nullopt;
}

std::optional<float> rayVsSphere(const glm::vec3 &o, const glm::vec3 &d, const glm::vec3 &center,
                                 float radius, float maxDistance) {
  const glm::vec3 oc = o - center;
  const float b = glm::dot(oc, d);
  const float c = glm::dot(oc, oc) - radius * radius;
  const float disc = b * b - c;
  if (disc < 0.0F) {
    return std::nullopt;
  }
  const float t = -b - std::sqrt(disc);
  return t > 0.0F && t <= maxDistance ? std::optional<float>(t) : std::nullopt;
}

} // namespace

MapEditor::MapEditor(MapData map) : m_map(std::move(map)) {}

void MapEditor::load(MapData map) {
  m_map = std::move(map);
  m_selection = {};
  m_undo.clear();
  m_redo.clear();
  m_groupDepth = 0;
  m_groupRecorded = false;
  m_dirty = false;
}

bool MapEditor::exists(Selection s) const {
  if (!s.valid()) {
    return false;
  }
  const std::size_t n = s.type == Selection::Type::Brush ? m_map.brushes.size() : m_map.entities.size();
  return static_cast<std::size_t>(s.index) < n;
}

void MapEditor::select(Selection s) { m_selection = exists(s) ? s : Selection{}; }

void MapEditor::record() {
  if (m_groupDepth > 0) {
    if (m_groupRecorded) {
      return;
    }
    m_groupRecorded = true;
  }
  m_undo.push_back({m_map, m_selection});
  if (m_undo.size() > kMaxUndo) {
    m_undo.erase(m_undo.begin());
  }
  m_redo.clear();
}

void MapEditor::beginGroup() {
  if (m_groupDepth++ == 0) {
    m_groupRecorded = false;
  }
}

void MapEditor::endGroup() {
  if (m_groupDepth > 0) {
    --m_groupDepth;
  }
}

int MapEditor::addBox(const glm::vec3 &mins, const glm::vec3 &maxs, std::string material) {
  record();
  touch();
  m_map.brushes.push_back(MapBrush::box(mins, maxs, std::move(material)));
  m_selection = {Selection::Type::Brush, static_cast<int>(m_map.brushes.size()) - 1};
  return m_selection.index;
}

int MapEditor::addRamp(const RampDef &ramp, std::string material) {
  record();
  touch();
  m_map.brushes.push_back(MapBrush::makeRamp(ramp, std::move(material)));
  m_selection = {Selection::Type::Brush, static_cast<int>(m_map.brushes.size()) - 1};
  return m_selection.index;
}

int MapEditor::addEntity(std::string type, const glm::vec3 &origin, float yaw) {
  record();
  touch();
  m_map.entities.push_back({std::move(type), origin, yaw});
  m_selection = {Selection::Type::Entity, static_cast<int>(m_map.entities.size()) - 1};
  return m_selection.index;
}

void MapEditor::remove(Selection s) {
  if (!exists(s)) {
    return;
  }
  record();
  touch();
  if (s.type == Selection::Type::Brush) {
    m_map.brushes.erase(m_map.brushes.begin() + s.index);
  } else {
    m_map.entities.erase(m_map.entities.begin() + s.index);
  }
  m_selection = {};
}

Selection MapEditor::duplicate(Selection s, const glm::vec3 &offset) {
  if (!exists(s)) {
    return {};
  }
  record();
  touch();
  if (s.type == Selection::Type::Brush) {
    m_map.brushes.push_back(m_map.brushes[static_cast<std::size_t>(s.index)]);
    m_selection = {Selection::Type::Brush, static_cast<int>(m_map.brushes.size()) - 1};
  } else {
    m_map.entities.push_back(m_map.entities[static_cast<std::size_t>(s.index)]);
    m_selection = {Selection::Type::Entity, static_cast<int>(m_map.entities.size()) - 1};
  }
  // The copy is already selected; shift it as part of the same undo step.
  beginGroup();
  m_groupRecorded = true;
  move(m_selection, offset);
  endGroup();
  return m_selection;
}

void MapEditor::move(Selection s, const glm::vec3 &delta) {
  if (!exists(s) || delta == glm::vec3(0.0F)) {
    return;
  }
  record();
  touch();
  if (s.type == Selection::Type::Entity) {
    m_map.entities[static_cast<std::size_t>(s.index)].origin += delta;
    return;
  }
  MapBrush &b = m_map.brushes[static_cast<std::size_t>(s.index)];
  switch (b.kind) {
  case BrushKind::Box:
    b.mins += delta;
    b.maxs += delta;
    (void)b.rebuild();
    break;
  case BrushKind::Ramp:
    b.ramp.min += glm::vec2(delta.x, delta.z);
    b.ramp.max += glm::vec2(delta.x, delta.z);
    b.ramp.low += delta.y;
    b.ramp.high += delta.y;
    b.ramp.bottom += delta.y;
    (void)b.rebuild();
    break;
  case BrushKind::Planes:
    for (Plane &p : b.brush.planes) {
      p.dist += glm::dot(p.normal, delta);
    }
    break;
  }
}

std::expected<void, Error> MapEditor::setBox(int brush, const glm::vec3 &mins, const glm::vec3 &maxs) {
  if (!exists({Selection::Type::Brush, brush}) ||
      m_map.brushes[static_cast<std::size_t>(brush)].kind != BrushKind::Box) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, "not a box brush"});
  }
  MapBrush candidate = m_map.brushes[static_cast<std::size_t>(brush)];
  candidate.mins = mins;
  candidate.maxs = maxs;
  if (auto r = candidate.rebuild(); !r) {
    return r;
  }
  record();
  touch();
  m_map.brushes[static_cast<std::size_t>(brush)] = std::move(candidate);
  return {};
}

std::expected<void, Error> MapEditor::setRamp(int brush, const RampDef &ramp) {
  if (!exists({Selection::Type::Brush, brush}) ||
      m_map.brushes[static_cast<std::size_t>(brush)].kind != BrushKind::Ramp) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, "not a ramp brush"});
  }
  MapBrush candidate = m_map.brushes[static_cast<std::size_t>(brush)];
  candidate.ramp = ramp;
  if (auto r = candidate.rebuild(); !r) {
    return r;
  }
  record();
  touch();
  m_map.brushes[static_cast<std::size_t>(brush)] = std::move(candidate);
  return {};
}

void MapEditor::setMaterial(int brush, std::string material) {
  if (!exists({Selection::Type::Brush, brush}) ||
      m_map.brushes[static_cast<std::size_t>(brush)].material == material) {
    return;
  }
  record();
  touch();
  m_map.brushes[static_cast<std::size_t>(brush)].material = std::move(material);
}

void MapEditor::setEntity(int entity, std::string type, const glm::vec3 &origin, float yaw) {
  if (!exists({Selection::Type::Entity, entity})) {
    return;
  }
  MapEntity &e = m_map.entities[static_cast<std::size_t>(entity)];
  if (e.type == type && e.origin == origin && e.yaw == yaw) {
    return;
  }
  record();
  touch();
  e = {std::move(type), origin, yaw};
}

bool MapEditor::undo() {
  if (m_undo.empty()) {
    return false;
  }
  m_redo.push_back({m_map, m_selection});
  m_map = std::move(m_undo.back().map);
  m_selection = m_undo.back().selection;
  m_undo.pop_back();
  m_groupRecorded = false;
  touch();
  return true;
}

bool MapEditor::redo() {
  if (m_redo.empty()) {
    return false;
  }
  m_undo.push_back({m_map, m_selection});
  m_map = std::move(m_redo.back().map);
  m_selection = m_redo.back().selection;
  m_redo.pop_back();
  m_groupRecorded = false;
  touch();
  return true;
}

std::optional<PickHit> MapEditor::pick(const glm::vec3 &origin, const glm::vec3 &dir,
                                       float maxDistance) const {
  std::optional<PickHit> best;
  const auto consider = [&](Selection what, float t) {
    if (!best || t < best->distance) {
      best = PickHit{what, t};
    }
  };
  for (std::size_t i = 0; i < m_map.brushes.size(); ++i) {
    if (const auto t = rayVsBrush(m_map.brushes[i].brush, origin, dir, maxDistance)) {
      consider({Selection::Type::Brush, static_cast<int>(i)}, *t);
    }
  }
  for (std::size_t i = 0; i < m_map.entities.size(); ++i) {
    const glm::vec3 center = m_map.entities[i].origin + glm::vec3(0.0F, kEntityCenterHeight, 0.0F);
    if (const auto t = rayVsSphere(origin, dir, center, kEntityRadius, maxDistance)) {
      consider({Selection::Type::Entity, static_cast<int>(i)}, *t);
    }
  }
  return best;
}

std::optional<std::pair<glm::vec3, glm::vec3>> MapEditor::bounds(Selection s) const {
  if (!exists(s)) {
    return std::nullopt;
  }
  if (s.type == Selection::Type::Entity) {
    const glm::vec3 o = m_map.entities[static_cast<std::size_t>(s.index)].origin;
    return std::pair{o - glm::vec3(16.0F, 0.0F, 16.0F), o + glm::vec3(16.0F, 72.0F, 16.0F)};
  }
  const MapBrush &b = m_map.brushes[static_cast<std::size_t>(s.index)];
  if (b.kind == BrushKind::Box) {
    return std::pair{b.mins, b.maxs};
  }
  if (b.kind == BrushKind::Ramp) {
    return std::pair{glm::vec3(b.ramp.min.x, b.ramp.bottom, b.ramp.min.y),
                     glm::vec3(b.ramp.max.x, std::max(b.ramp.low, b.ramp.high), b.ramp.max.y)};
  }
  // Plane brushes: the corners of the clipped faces.
  glm::vec3 lo(std::numeric_limits<float>::max());
  glm::vec3 hi(std::numeric_limits<float>::lowest());
  for (const BrushFace &f : brushFaces(b.brush)) {
    for (const glm::vec3 &v : f.vertices) {
      lo = glm::min(lo, v);
      hi = glm::max(hi, v);
    }
  }
  return std::pair{lo, hi};
}

std::vector<MapIssue> MapEditor::validate() const {
  std::vector<MapIssue> issues;
  const auto collision = m_map.buildCollision();
  if (!collision) {
    issues.push_back({"collision cannot be built: " + collision.error().message, true});
  }
  int spawns = 0;
  int spawnsT = 0;
  int spawnsCT = 0;
  for (std::size_t i = 0; i < m_map.entities.size(); ++i) {
    const MapEntity &e = m_map.entities[i];
    const std::string name = "entity " + std::to_string(i) + " (" + e.type + ")";
    const bool known = std::any_of(std::begin(kEntityTypes), std::end(kEntityTypes),
                                   [&](const char *t) { return e.type == t; });
    if (!known) {
      issues.push_back({name + ": unknown type, the game ignores it", false});
    }
    spawns += e.type == "spawn" ? 1 : 0;
    spawnsT += e.type == "spawn_t" ? 1 : 0;
    spawnsCT += e.type == "spawn_ct" ? 1 : 0;
    if (collision && known) {
      if (collision->traceHull(e.origin, e.origin, kStandingHull).startSolid) {
        issues.push_back({name + ": stuck inside a brush", false});
      } else {
        const auto down = collision->traceHull(e.origin, e.origin - glm::vec3(0, 400, 0), kStandingHull);
        if (down.fraction >= 1.0F) {
          issues.push_back({name + ": nothing below it (it would fall out of the map)", false});
        }
      }
    }
  }
  if (spawns + spawnsT + spawnsCT == 0) {
    issues.push_back({"the map has no spawn points", false});
  }
  if ((spawnsT > 0) != (spawnsCT > 0)) {
    issues.push_back({"team spawns exist for only one team", false});
  }
  if (m_map.brushes.empty()) {
    issues.push_back({"the map has no brushes", false});
  }
  return issues;
}
} // namespace Paper
