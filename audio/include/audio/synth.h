#pragma once

#include <cstdint>
#include <vector>

// Placeholder sounds generated from noise and sine waves (no asset files), so
// the game has audio feedback until real recordings exist. Deterministic.
namespace Paper::Synth {
enum class ShotKind { Rifle, Pistol, Sniper, Knife };

[[nodiscard]] std::vector<float> shot(ShotKind kind, std::uint32_t sampleRate);
[[nodiscard]] std::vector<float> footstep(std::uint32_t sampleRate);
[[nodiscard]] std::vector<float> impact(std::uint32_t sampleRate);
[[nodiscard]] std::vector<float> hitMarker(std::uint32_t sampleRate);
[[nodiscard]] std::vector<float> reload(std::uint32_t sampleRate);
} // namespace Paper::Synth
