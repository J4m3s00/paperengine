#pragma once

#include "game/hitbox.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Paper {
// Remembers every player's pose for the last `kHistoryTicks` ticks so the
// server can put targets where the shooter saw them (ADR 0009/0011).
class LagCompensation {
public:
  static constexpr std::size_t kSlots = 16;
  static constexpr std::size_t kHistoryTicks = 128;

  // Call once per server tick, after movement, for every living player.
  void record(std::uint32_t tick, std::uint8_t slot, const PlayerPose &pose);
  // Forget a slot (death, leaving) so no stale poses are interpolated.
  void clear(std::uint8_t slot);

  // The pose of `slot` at a fractional tick, interpolated between recorded
  // ticks. Null if the slot has no data near that time.
  [[nodiscard]] std::optional<PlayerPose> poseAt(std::uint8_t slot, double tick) const;

  // Limits how far back a client may rewind: not into the future and not
  // more than `maxRewindTicks` before `currentTick`.
  [[nodiscard]] static double clampViewTick(double viewTick, std::uint32_t currentTick,
                                            std::uint32_t maxRewindTicks);

private:
  struct Entry {
    std::uint32_t tick = 0;
    bool valid = false;
    PlayerPose pose;
  };
  [[nodiscard]] const Entry *entry(std::uint8_t slot, std::uint32_t tick) const;

  std::array<std::array<Entry, kHistoryTicks>, kSlots> m_history{};
};
} // namespace Paper
