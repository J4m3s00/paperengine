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

RampDef rampDefOf(const Json &r) {
  if (!r.contains("min") || !r.contains("max") || !r.contains("low") || !r.contains("high") ||
      !r.contains("rising")) {
    fail("ramp needs min, max, low, high and rising");
  }
  RampDef d;
  d.min = {r["min"].at(0).get<float>(), r["min"].at(1).get<float>()};
  d.max = {r["max"].at(0).get<float>(), r["max"].at(1).get<float>()};
  d.low = r["low"].get<float>();
  d.high = r["high"].get<float>();
  d.bottom = r.value("bottom", std::min(d.low, d.high) - 64.0F);
  d.rising = r["rising"].get<std::string>();
  return d;
}
MapBrush parseBrush(const Json &j) {
  MapBrush out;
  out.material = j.value("material", std::string());
  if (j.contains("box")) {
    out.kind = BrushKind::Box;
    const Json &b = j["box"];
    if (!b.is_array() || b.size() != 6) {
      fail("box must be [minX, minY, minZ, maxX, maxY, maxZ]");
    }
    const glm::vec3 mins(b[0].get<float>(), b[1].get<float>(), b[2].get<float>());
    const glm::vec3 maxs(b[3].get<float>(), b[4].get<float>(), b[5].get<float>());
    if (maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
      fail("box max must be greater than min on every axis");
    }
    out.mins = mins;
    out.maxs = maxs;
  } else if (j.contains("ramp")) {
    out.kind = BrushKind::Ramp;
    out.ramp = rampDefOf(j["ramp"]);
  } else if (j.contains("planes")) {
    out.kind = BrushKind::Planes;
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
  if (auto built = out.rebuild(); !built) {
    fail(built.error().message);
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

std::expected<BrushDesc, Error> rampPlanes(const RampDef &r) {
  const auto invalid = [](const char *m) { return std::unexpected(Error{ErrorCode::Asset, m}); };
  glm::vec3 u(0.0F);
  float lowCoord = 0.0F; // u . (the low end of the ramp)
  float length = 0.0F;
  if (r.rising == "+x") {
    u = {1, 0, 0};
    lowCoord = r.min.x;
    length = r.max.x - r.min.x;
  } else if (r.rising == "-x") {
    u = {-1, 0, 0};
    lowCoord = -r.max.x;
    length = r.max.x - r.min.x;
  } else if (r.rising == "+z") {
    u = {0, 0, 1};
    lowCoord = r.min.y;
    length = r.max.y - r.min.y;
  } else if (r.rising == "-z") {
    u = {0, 0, -1};
    lowCoord = -r.max.y;
    length = r.max.y - r.min.y;
  } else {
    return invalid("ramp rising must be +x, -x, +z or -z");
  }
  if (length <= 0.0F || r.max.x <= r.min.x || r.max.y <= r.min.y) {
    return invalid("ramp max must be greater than min");
  }
  if (r.bottom >= std::min(r.low, r.high)) {
    return invalid("ramp bottom must be below the ramp");
  }
  // Surface: y = low + slope * (u . p - lowCoord)  ->  n . p = dist
  const float slope = (r.high - r.low) / length;
  const glm::vec3 n(-slope * u.x, 1.0F, -slope * u.z);
  const float dist = r.low - slope * lowCoord;
  return BrushDesc{{{n, dist},
                    {{0, -1, 0}, -r.bottom},
                    {{1, 0, 0}, r.max.x},
                    {{-1, 0, 0}, -r.min.x},
                    {{0, 0, 1}, r.max.y},
                    {{0, 0, -1}, -r.min.y}}};
}

MapBrush MapBrush::box(const glm::vec3 &mins, const glm::vec3 &maxs, std::string material) {
  MapBrush b;
  b.kind = BrushKind::Box;
  b.mins = mins;
  b.maxs = maxs;
  b.material = std::move(material);
  b.brush = boxBrush(mins, maxs);
  return b;
}

MapBrush MapBrush::makeRamp(const RampDef &ramp, std::string material) {
  MapBrush b;
  b.kind = BrushKind::Ramp;
  b.ramp = ramp;
  b.material = std::move(material);
  if (auto planes = rampPlanes(ramp)) {
    b.brush = std::move(*planes);
  }
  return b;
}

std::expected<void, Error> MapBrush::rebuild() {
  switch (kind) {
  case BrushKind::Box:
    if (maxs.x <= mins.x || maxs.y <= mins.y || maxs.z <= mins.z) {
      return std::unexpected(
          Error{ErrorCode::Asset, "box max must be greater than min on every axis"});
    }
    brush = boxBrush(mins, maxs);
    return {};
  case BrushKind::Ramp: {
    auto planes = rampPlanes(ramp);
    if (!planes) {
      return std::unexpected(planes.error());
    }
    brush = std::move(*planes);
    return {};
  }
  case BrushKind::Planes:
    return {};
  }
  return {};
}

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

namespace Paper {
std::string serializeMap(const MapData &map) {
  Json root;
  root["version"] = 1;
  Json brushes = Json::array();
  for (const MapBrush &b : map.brushes) {
    Json j;
    switch (b.kind) {
    case BrushKind::Box:
      j["box"] = {b.mins.x, b.mins.y, b.mins.z, b.maxs.x, b.maxs.y, b.maxs.z};
      break;
    case BrushKind::Ramp:
      j["ramp"] = {{"min", {b.ramp.min.x, b.ramp.min.y}},
                   {"max", {b.ramp.max.x, b.ramp.max.y}},
                   {"low", b.ramp.low},
                   {"high", b.ramp.high},
                   {"bottom", b.ramp.bottom},
                   {"rising", b.ramp.rising}};
      break;
    case BrushKind::Planes: {
      Json planes = Json::array();
      for (const Plane &p : b.brush.planes) {
        planes.push_back({p.normal.x, p.normal.y, p.normal.z, p.dist});
      }
      j["planes"] = planes;
      break;
    }
    }
    if (!b.material.empty()) {
      j["material"] = b.material;
    }
    brushes.push_back(std::move(j));
  }
  root["brushes"] = std::move(brushes);
  Json entities = Json::array();
  for (const MapEntity &e : map.entities) {
    entities.push_back({{"type", e.type},
                        {"origin", {e.origin.x, e.origin.y, e.origin.z}},
                        {"yaw", glm::degrees(e.yaw)}});
  }
  root["entities"] = std::move(entities);
  return root.dump(1);
}

std::expected<void, Error> saveMap(const MapData &map, const std::filesystem::path &path) {
  // Write next to the target first so a crash never leaves half a map.
  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream file(temp);
    if (!file) {
      return std::unexpected(Error{ErrorCode::Io, "cannot write '" + temp.string() + "'"});
    }
    file << serializeMap(map) << "\n";
    if (!file) {
      return std::unexpected(Error{ErrorCode::Io, "write failed for '" + temp.string() + "'"});
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    return std::unexpected(Error{ErrorCode::Io, "cannot replace '" + path.string() + "'"});
  }
  return {};
}
} // namespace Paper
