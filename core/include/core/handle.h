#pragma once

#include <cstdint>

namespace Paper {
// Typed reference to a resource owned by the Renderer (see ADR 0003). The
// generation detects handles whose slot has been reused.
template <typename Tag> struct Handle {
  static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFU;

  std::uint32_t index = kInvalidIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] bool valid() const { return index != kInvalidIndex; }
  friend bool operator==(const Handle &, const Handle &) = default;
};
} // namespace Paper
