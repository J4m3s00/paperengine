#pragma once

#include "base/error.h"
#include "game/collision.h"

#include <glm/glm.hpp>
#include <glm/vec2.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Paper {
enum class BrushKind { Box, Ramp, Planes };

// A sloped slab, as authored (see the "ramp" brush of the JSON format).
struct RampDef {
  glm::vec2 min{0.0F};   // x, z
  glm::vec2 max{64.0F};  // x, z
  float low = 0.0F;      // surface height at the low end
  float high = 32.0F;    // surface height at the high end
  float bottom = -64.0F; // underside
  // The direction the surface rises in: "+x", "-x", "+z" or "-z".
  std::string rising = "+x";
};

// Plane set of a ramp, or an error if the definition is degenerate.
[[nodiscard]] std::expected<BrushDesc, Error> rampPlanes(const RampDef &ramp);

struct MapBrush {
  // The solid, always in sync with the authored shape below (`rebuild()`).
  BrushDesc brush;
  // Free-form name used by tools and the renderer to pick a look.
  std::string material;
  // How the brush was authored; editors keep editing that shape.
  BrushKind kind = BrushKind::Planes;
  glm::vec3 mins{0.0F}; // Box
  glm::vec3 maxs{0.0F}; // Box
  RampDef ramp;         // Ramp

  [[nodiscard]] static MapBrush box(const glm::vec3 &mins, const glm::vec3 &maxs,
                                    std::string material = {});
  [[nodiscard]] static MapBrush makeRamp(const RampDef &ramp, std::string material = {});
  // Recomputes `brush` from the box/ramp shape. Fails for a degenerate shape.
  [[nodiscard]] std::expected<void, Error> rebuild();
};

// A point entity: "spawn", "bot", ...
struct MapEntity {
  std::string type;
  glm::vec3 origin{0.0F};
  // Radians, same convention as UserCmd::yaw.
  float yaw = 0.0F;
};

// A level as authored: brushes plus entities. Units are Source units, Y up.
struct MapData {
  std::vector<MapBrush> brushes;
  std::vector<MapEntity> entities;

  [[nodiscard]] std::vector<BrushDesc> brushDescs() const;
  [[nodiscard]] std::expected<CollisionWorld, Error> buildCollision() const;
  [[nodiscard]] std::vector<MapEntity> entitiesOfType(std::string_view type) const;
};

// JSON map format, version 1:
//   { "version": 1,
//     "brushes": [ { "box": [minX, minY, minZ, maxX, maxY, maxZ] },
//                  { "ramp": { "min": [x, z], "max": [x, z], "low": y, "high": y,
//                              "rising": "+x"|"-x"|"+z"|"-z", "bottom": y } },
//                  { "planes": [[nx, ny, nz, dist], ...] } ],   // + optional "material"
//     "entities": [ { "type": "spawn", "origin": [x, y, z], "yaw": degrees } ] }
[[nodiscard]] std::expected<MapData, Error> parseMap(std::string_view json);
[[nodiscard]] std::expected<MapData, Error> loadMap(const std::filesystem::path &path);

// The inverse of parseMap: JSON v1 with box/ramp/planes brushes as authored.
[[nodiscard]] std::string serializeMap(const MapData &map);
[[nodiscard]] std::expected<void, Error> saveMap(const MapData &map,
                                                 const std::filesystem::path &path);
} // namespace Paper
