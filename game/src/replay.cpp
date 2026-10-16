#include "game/replay.h"

#include "game/tick.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace Paper {
namespace {
using Json = nlohmann::json;

constexpr int kJump = 1;
constexpr int kDuck = 2;
constexpr int kWalk = 4;

Json vec(const glm::vec3 &v) { return Json::array({v.x, v.y, v.z}); }
glm::vec3 vecOf(const Json &j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }

Json stateJson(const PlayerState &s) {
  return {{"origin", vec(s.origin)}, {"velocity", vec(s.velocity)}, {"onGround", s.onGround},
          {"ducked", s.ducked},      {"jumpHeld", s.jumpHeld}};
}

PlayerState stateOf(const Json &j) {
  PlayerState s;
  s.origin = vecOf(j.at("origin"));
  s.velocity = vecOf(j.at("velocity"));
  s.onGround = j.at("onGround").get<bool>();
  s.ducked = j.at("ducked").get<bool>();
  s.jumpHeld = j.at("jumpHeld").get<bool>();
  return s;
}

Json paramsJson(const MovementParams &p) {
  return {{"accelerate", p.accelerate},
          {"airAccelerate", p.airAccelerate},
          {"friction", p.friction},
          {"stopSpeed", p.stopSpeed},
          {"gravity", p.gravity},
          {"jumpImpulse", p.jumpImpulse},
          {"stepSize", p.stepSize},
          {"walkableNormal", p.walkableNormal},
          {"maxVelocity", p.maxVelocity},
          {"duckSpeedFactor", p.duckSpeedFactor},
          {"walkSpeedFactor", p.walkSpeedFactor},
          {"airSpeedCap", p.airSpeedCap},
          {"enableBunnyhopping", p.enableBunnyhopping},
          {"bunnyJumpCap", p.bunnyJumpCap},
          {"airDuckLift", p.airDuckLift}};
}

MovementParams paramsOf(const Json &j) {
  MovementParams p;
  p.accelerate = j.at("accelerate").get<float>();
  p.airAccelerate = j.at("airAccelerate").get<float>();
  p.friction = j.at("friction").get<float>();
  p.stopSpeed = j.at("stopSpeed").get<float>();
  p.gravity = j.at("gravity").get<float>();
  p.jumpImpulse = j.at("jumpImpulse").get<float>();
  p.stepSize = j.at("stepSize").get<float>();
  p.walkableNormal = j.at("walkableNormal").get<float>();
  p.maxVelocity = j.at("maxVelocity").get<float>();
  p.duckSpeedFactor = j.at("duckSpeedFactor").get<float>();
  p.walkSpeedFactor = j.at("walkSpeedFactor").get<float>();
  p.airSpeedCap = j.at("airSpeedCap").get<float>();
  p.enableBunnyhopping = j.at("enableBunnyhopping").get<bool>();
  p.bunnyJumpCap = j.at("bunnyJumpCap").get<float>();
  p.airDuckLift = j.at("airDuckLift").get<float>();
  return p;
}
} // namespace

PlayerState runReplay(const Replay &replay, const CollisionWorld &world,
                      std::vector<PlayerState> *trajectory) {
  PlayerState state = replay.start;
  if (trajectory != nullptr) {
    trajectory->clear();
    trajectory->reserve(replay.cmds.size());
  }
  for (const UserCmd &cmd : replay.cmds) {
    movePlayer(state, cmd, world, replay.params, replay.maxSpeed,
               static_cast<float>(kServerTickSeconds));
    if (trajectory != nullptr) {
      trajectory->push_back(state);
    }
  }
  return state;
}

std::string serializeReplay(const Replay &replay) {
  Json cmds = Json::array();
  for (const UserCmd &c : replay.cmds) {
    const int buttons = (c.jump ? kJump : 0) | (c.duck ? kDuck : 0) | (c.walk ? kWalk : 0);
    cmds.push_back(Json::array({c.yaw, c.pitch, c.forward, c.side, buttons}));
  }
  Json root = {{"version", 1},
               {"map", replay.map},
               {"tickRate", kServerTickRate},
               {"maxSpeed", replay.maxSpeed},
               {"start", stateJson(replay.start)},
               {"params", paramsJson(replay.params)},
               {"cmds", std::move(cmds)}};
  if (replay.expected) {
    root["expected"] = stateJson(*replay.expected);
  }
  // One command per line keeps diffs of recordings readable.
  std::string out = "{\n";
  for (auto it = root.begin(); it != root.end(); ++it) {
    out += "  " + Json(it.key()).dump() + ": ";
    if (it.key() == "cmds") {
      out += "[\n";
      for (std::size_t i = 0; i < it.value().size(); ++i) {
        out += "    " + it.value()[i].dump() + (i + 1 < it.value().size() ? ",\n" : "\n");
      }
      out += "  ]";
    } else {
      out += it.value().dump();
    }
    out += std::next(it) != root.end() ? ",\n" : "\n";
  }
  out += "}\n";
  return out;
}

std::expected<Replay, Error> parseReplay(std::string_view text) {
  const auto invalid = [](std::string message) {
    return std::unexpected(Error{ErrorCode::Asset, "replay: " + std::move(message)});
  };
  try {
    const Json root = Json::parse(text);
    if (root.value("version", 0) != 1) {
      return invalid("unsupported or missing \"version\"");
    }
    if (root.value("tickRate", 0U) != kServerTickRate) {
      return invalid("recorded at a different tick rate");
    }
    Replay replay;
    replay.map = root.value("map", std::string());
    replay.maxSpeed = root.at("maxSpeed").get<float>();
    replay.start = stateOf(root.at("start"));
    replay.params = paramsOf(root.at("params"));
    for (const Json &c : root.at("cmds")) {
      UserCmd cmd;
      cmd.yaw = c.at(0).get<float>();
      cmd.pitch = c.at(1).get<float>();
      cmd.forward = c.at(2).get<float>();
      cmd.side = c.at(3).get<float>();
      const int buttons = c.at(4).get<int>();
      cmd.jump = (buttons & kJump) != 0;
      cmd.duck = (buttons & kDuck) != 0;
      cmd.walk = (buttons & kWalk) != 0;
      replay.cmds.push_back(cmd);
    }
    if (root.contains("expected")) {
      replay.expected = stateOf(root["expected"]);
    }
    return replay;
  } catch (const Json::exception &e) {
    return invalid(e.what());
  }
}

std::expected<Replay, Error> loadReplay(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return std::unexpected(Error{ErrorCode::Io, "cannot open replay '" + path.string() + "'"});
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return parseReplay(buffer.str());
}

std::expected<void, Error> saveReplay(const std::filesystem::path &path, const Replay &replay) {
  std::ofstream file(path);
  if (!file) {
    return std::unexpected(Error{ErrorCode::Io, "cannot write replay '" + path.string() + "'"});
  }
  file << serializeReplay(replay);
  return {};
}
} // namespace Paper
