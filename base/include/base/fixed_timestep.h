#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Paper {
// Accumulates real time and hands out fixed simulation steps. The caller owns
// the clock and passes elapsed seconds, so this is testable without real time.
//
//   stepper.advance(dt);
//   while (stepper.consumeStep()) simulate(stepper.stepSeconds());
//   render(stepper.alpha());
class FixedTimestep {
public:
  // `maxSteps` bounds catch-up after a stall; excess time is dropped so the
  // simulation cannot spiral (each step taking longer than it advances).
  explicit FixedTimestep(double stepSeconds, std::uint32_t maxSteps = 8)
      : m_step(toNanos(stepSeconds)), m_maxSteps(maxSteps) {}

  void advance(double elapsedSeconds) {
    m_accumulator += std::max<std::int64_t>(toNanos(elapsedSeconds), 0);
    const std::int64_t limit = m_step * m_maxSteps;
    if (m_accumulator > limit)
      m_accumulator = limit;
  }

  // True and consumes one step while a full step is available.
  [[nodiscard]] bool consumeStep() {
    if (m_accumulator < m_step)
      return false;
    m_accumulator -= m_step;
    ++m_tick;
    return true;
  }

  [[nodiscard]] double stepSeconds() const { return m_step * 1e-9; }
  // Steps consumed so far.
  [[nodiscard]] std::uint64_t tick() const { return m_tick; }
  // Fraction of the next step already elapsed, in [0, 1), for render interpolation.
  [[nodiscard]] double alpha() const { return static_cast<double>(m_accumulator) / static_cast<double>(m_step);
  }

private:
  // Integer nanoseconds keep repeated subtraction exact (no float drift).
  static std::int64_t toNanos(double seconds) {
    return static_cast<std::int64_t>(std::llround(seconds * 1e9));
  }

  std::int64_t m_step;
  std::uint32_t m_maxSteps;
  std::int64_t m_accumulator = 0;
  std::uint64_t m_tick = 0;
};
} // namespace Paper
