#pragma once

#include "base/error.h"
#include "game/collision.h"

#include <glm/glm.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Paper {
struct MapBrush {
  BrushDesc brush;
  // Free-form name used by tools and the renderer to pick a look.
  std::string material;
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
} // namespace Paper
