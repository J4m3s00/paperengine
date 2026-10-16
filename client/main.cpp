#include "game/tick.h"

#include <spdlog/spdlog.h>

int main() {
  spdlog::info("paper client stub: server tick rate {} Hz",
               Paper::kServerTickRate);
  return 0;
}
