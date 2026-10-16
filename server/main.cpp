#include "game/tick.h"
#include "net/protocol.h"

#include <spdlog/spdlog.h>

int main() {
  spdlog::info("paper server stub: tick rate {} Hz, port {}, {} slots",
               Paper::kServerTickRate, Paper::kDefaultServerPort,
               Paper::kMaxPlayerSlots);
  return 0;
}
