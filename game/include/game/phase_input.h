#pragma once

#include "game/movement.h"
#include "game/rules.h"

namespace Paper {
// The input a player may actually give in a phase: during freeze only looking
// around works. The server enforces it and the client predicts the same.
[[nodiscard]] inline UserCmd restrictCmd(UserCmd cmd, Phase phase) {
  if (!phaseAllowsMovement(phase)) {
    cmd.forward = 0.0F;
    cmd.side = 0.0F;
    cmd.jump = false;
    cmd.duck = false;
  }
  return cmd;
}
} // namespace Paper
