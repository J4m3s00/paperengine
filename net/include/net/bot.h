#pragma once

#include "game/movement.h"

#include <cstdint>

namespace Paper {
// Scripted input for load and soak tests: walks in random directions, turns,
// jumps and crouches. Deterministic for a given seed.
class BotBrain {
public:
  explicit BotBrain(std::uint64_t seed) : m_state(seed * 0x9E3779B97F4A7C15ULL + 1) {}

  [[nodiscard]] UserCmd next() {
    if (m_ticksLeft <= 0) {
      pickAction();
    }
    --m_ticksLeft;
    m_yaw += m_turn;
    UserCmd c;
    c.yaw = m_yaw;
    c.forward = m_forward;
    c.side = m_side;
    c.duck = m_duck;
    c.walk = m_walk;
    c.jump = m_jumpEvery > 0 && (m_ticksLeft % m_jumpEvery) == 0;
    c.attack = m_shooting;
    c.reload = m_reloadAt == m_ticksLeft;
    c.weaponSelect = m_ticksLeft == 1 ? m_nextWeapon : 0;
    return c;
  }

private:
  std::uint64_t random() {
    m_state ^= m_state << 13;
    m_state ^= m_state >> 7;
    m_state ^= m_state << 17;
    return m_state;
  }
  float pick(float a, float b, float c) {
    const auto r = random() % 3;
    return r == 0 ? a : (r == 1 ? b : c);
  }
  void pickAction() {
    m_ticksLeft = 32 + static_cast<int>(random() % 160);
    m_forward = pick(1.0F, 1.0F, -1.0F);
    m_side = pick(-1.0F, 0.0F, 1.0F);
    m_turn = (static_cast<float>(random() % 2001) / 1000.0F - 1.0F) * 0.01F;
    m_duck = random() % 8 == 0;
    m_walk = random() % 8 == 0;
    m_jumpEvery = random() % 3 == 0 ? 40 : 0;
    m_shooting = random() % 2 == 0;
    m_reloadAt = random() % 4 == 0 ? 10 : -1;
    m_nextWeapon = random() % 3 == 0 ? static_cast<std::uint8_t>(1 + random() % 7) : 0;
  }

  std::uint64_t m_state;
  int m_ticksLeft = 0;
  float m_yaw = 0.0F;
  float m_turn = 0.0F;
  float m_forward = 0.0F;
  float m_side = 0.0F;
  bool m_duck = false;
  bool m_walk = false;
  int m_jumpEvery = 0;
  bool m_shooting = false;
  int m_reloadAt = -1;
  std::uint8_t m_nextWeapon = 0;
};
} // namespace Paper
