#include "game/map.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

namespace Paper {
namespace {
using Json = nlohmann::json;

struct ParseError {
  std::string message;
};

[[noreturn]] void fail(const std::string &message) { throw ParseError{message}; }

glm::vec3 vec3Of(const Json &j, const char *what) {
  if (!j.is_array() || j.size() != 3) {
    fail(std::string(what) + " must be an array of 3 numbers");
  }
  return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

BrushDesc rampBrush(const Json &r) {
  if (!r.contains("min") || !r.contains("max") || !r.contains("low") || !r.contains("high") ||
      !r.contains("rising")) {
    fail("ramp needs min, max, low, high and rising");
  }
  const float minX = r["min"].at(0).get<float>();
  const float minZ = r["min"].at(1).get<float>();
  const float maxX = r["max"].at(0).get<float>();
  const float maxZ = r["max"].at(1).get<float>();
  const float low = r["low"].get<float>();
  const float high = r["high"].get<float>();
  const float bottom = r.value("bottom", std::min(low, high) - 64.0F);
  const std::string rising = r["rising"].get<std::string>();

  glm::vec3 u(0.0F);
  float lowCoord = 0.0F; // u . (the low end of the ramp)
  float length = 0.0F;
  if (rising == "+x") {
    u = {1, 0, 0};
    lowCoord = minX;
    length = maxX - minX;
  } else if (rising == "-x") {
    u = {-1, 0, 0};
    lowCoord = -maxX;
    length = maxX - minX;
  } else if (rising == "+z") {
    u = {0, 0, 1};
    lowCoord = minZ;
    length = maxZ - minZ;
  } else if (rising == "-z") {
    u = {0, 0, -1};
    lowCoord = -maxZ;
    length = maxZ - minZ;
  } else {
    fail("ramp rising must be +x, -x, +z or -z");
  }
  if (length <= 0.0F || maxX <= minX || maxZ <= minZ) {
    fail("ramp max must be greater than min");
  }
  if (bottom >= std::min(low, high)) {
    fail("ramp bottom must be below the ramp");
  }
  // Surface: y = low + slope * (u . p - lowCoord)  ->  n . p = dist
  const float slope = (high - low) / length;
  const glm::vec3 n(-slope * u.x, 1.0F, -slope * u.z);
  const float dist = low - slope * lowCoord;
  return BrushDesc{{{n, dist},
                    {{0, -1, 0}, -bottom},
                    {{1, 0, 0}, maxX},
                    {{-1, 0, 0}, -minX},
                    {{0, 0, 1}, maxZ},
                    {{0, 0, -1}, -minZ}}};
}

MapBrush parseBrush(const Json &j) {
  MapBrush out;
  out.material = j.value("material", std::string());
  if (j.contains("box")) {
    const Json &b = j["box"];
    if (!b.is_array() || b.size() != 6) {
      fail("box must be [minX, minY, minZ, maxX, maxY, maxZ]");
    }
    const glm::vec3 mins(b[0].get<float>(), b[1].get<float>(), b[2].get<float>());
    const glm::vec3 maxs(b[3].get<float>(), b[4].get<float>(), b[5].get<float>());
    if (maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
      fail("box max must be greater than min on every axis");
    }
    out.brush = boxBrush(mins, maxs);
  } else if (j.contains("ramp")) {
    out.brush = rampBrush(j["ramp"]);
  } else if (j.contains("planes")) {
    for (const Json &p : j["planes"]) {
      if (!p.is_array() || p.size() != 4) {
        fail("each plane must be [nx, ny, nz, dist]");
      }
      out.brush.planes.push_back(
          {{p[0].get<float>(), p[1].get<float>(), p[2].get<float>()}, p[3].get<float>()});
    }
  } else {
    fail("brush needs one of: box, ramp, planes");
  }
  return out;
}

MapEntity parseEntity(const Json &j) {
  MapEntity e;
  e.type = j.at("type").get<std::string>();
  e.origin = vec3Of(j.at("origin"), "origin");
  e.yaw = glm::radians(j.value("yaw", 0.0F));
  return e;
}
} // namespace

std::vector<BrushDesc> MapData::brushDescs() const {
  std::vector<BrushDesc> out;
  out.reserve(brushes.size());
  for (const MapBrush &b : brushes) {
    out.push_back(b.brush);
  }
  return out;
}

std::expected<CollisionWorld, Error> MapData::buildCollision() const {
  const std::vector<BrushDesc> descs = brushDescs();
  return CollisionWorld::create(descs);
}

std::vector<MapEntity> MapData::entitiesOfType(std::string_view type) const {
  std::vector<MapEntity> out;
  for (const MapEntity &e : entities) {
    if (e.type == type) {
      out.push_back(e);
    }
  }
  return out;
}

std::expected<MapData, Error> parseMap(std::string_view text) {
  const auto invalid = [](std::string message) {
    return std::unexpected(Error{ErrorCode::Asset, std::move(message)});
  };
  Json root;
  try {
    root = Json::parse(text);
  } catch (const Json::parse_error &e) {
    return invalid(std::string("map is not valid JSON: ") + e.what());
  }

  MapData map;
  std::string context = "map";
  try {
    if (root.value("version", 0) != 1) {
      fail("unsupported or missing \"version\" (expected 1)");
    }
    int index = 0;
    for (const Json &b : root.value("brushes", Json::array())) {
      context = "brush " + std::to_string(index++);
      map.brushes.push_back(parseBrush(b));
    }
    index = 0;
    for (const Json &e : root.value("entities", Json::array())) {
      context = "entity " + std::to_string(index++);
      map.entities.push_back(parseEntity(e));
    }
  } catch (const ParseError &e) {
    return invalid(context + ": " + e.message);
  } catch (const Json::exception &e) {
    return invalid(context + ": " + e.what());
  }
  return map;
}

std::expected<MapData, Error> loadMap(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return std::unexpected(Error{ErrorCode::Io, "cannot open map '" + path.string() + "'"});
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  auto map = parseMap(buffer.str());
  if (!map) {
    map.error().message = path.string() + ": " + map.error().message;
  }
  return map;
}
} // namespace Paper
