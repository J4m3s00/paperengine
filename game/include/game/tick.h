#pragma once

#include <cstdint>

namespace Paper {
// Simulation rate of the authoritative server (ADR 0009).
inline constexpr std::uint32_t kServerTickRate = 128;
inline constexpr double kServerTickSeconds = 1.0 / kServerTickRate;
} // namespace Paper
