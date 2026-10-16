#pragma once

namespace Paper {
// Extend as needed; mapped to GLFW key codes in window.cpp.
enum class Key {
  W,
  A,
  S,
  D,
  Q,
  E,
  Space,
  LeftShift,
  LeftControl,
  Escape,
  F1,
};

enum class MouseButton {
  Left,
  Right,
  Middle,
};

enum class CursorMode {
  Normal,
  // Hidden and captured: mouse movement keeps reporting deltas without limit.
  Captured,
};
} // namespace Paper
