#pragma once

#include "base/cvar.h"

#include <array>
#include <cstddef>

namespace Paper {
// Debug window for use inside `Renderer::setUiCallback`: frame-time graph with
// average and 1% low FPS, and live editing of every registered cvar.
class DebugOverlay {
public:
  explicit DebugOverlay(CvarRegistry &registry = cvars())
      : m_registry(registry) {}

  void setVisible(bool visible) { m_visible = visible; }
  void toggle() { m_visible = !m_visible; }
  [[nodiscard]] bool visible() const { return m_visible; }

  // Records this frame and draws the window if visible. Call once per frame
  // from the UI callback (frame timing is recorded even while hidden).
  void draw(float frameSeconds);

private:
  static constexpr std::size_t kHistory = 240;

  void drawFrameTimes() const;
  void drawCvars();

  CvarRegistry &m_registry;
  bool m_visible = false;
  std::array<float, kHistory> m_frameMs{};
  std::size_t m_next = 0;
  std::size_t m_count = 0;
};
} // namespace Paper
