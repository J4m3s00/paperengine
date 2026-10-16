#pragma once

#include "base/error.h"
#include "game/movement.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Paper {
// A recorded run of usercmds, replayable headless for regression tests: the
// same commands on the same map must end in the same state (within float
// drift across platforms, see ADR 0010).
struct Replay {
  // Map file name the replay was recorded on (informational).
  std::string map;
  PlayerState start;
  MovementParams params;
  float maxSpeed = kDefaultMaxSpeed;
  std::vector<UserCmd> cmds;
  // State after the last command when the replay was recorded or blessed.
  std::optional<PlayerState> expected;
};

// Runs every command through movePlayer at the server tick rate. If
// `trajectory` is given it receives the state after each tick.
[[nodiscard]] PlayerState runReplay(const Replay &replay, const CollisionWorld &world,
                                    std::vector<PlayerState> *trajectory = nullptr);

[[nodiscard]] std::string serializeReplay(const Replay &replay);
[[nodiscard]] std::expected<Replay, Error> parseReplay(std::string_view json);
[[nodiscard]] std::expected<Replay, Error> loadReplay(const std::filesystem::path &path);
[[nodiscard]] std::expected<void, Error> saveReplay(const std::filesystem::path &path,
                                                    const Replay &replay);
} // namespace Paper
