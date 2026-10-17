#include "base/cvar.h"
#include "base/fixed_timestep.h"
#include "game/map.h"
#include "net/enet_transport.h"
#include "net/server.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

void usage() {
  spdlog::info("usage: server [--map file.json] [--port N] [--snapshot-rate N] [--set cvar value]");
}
} // namespace

int main(int argc, char **argv) {
  std::string mapPath = std::string(PAPER_MAPS_DIR) + "/dev_world.json";
  std::uint16_t port = Paper::kDefaultServerPort;
  Paper::ServerConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--map" && hasValue) {
      mapPath = argv[++i];
    } else if (arg == "--port" && hasValue) {
      port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (arg == "--snapshot-rate" && hasValue) {
      config.snapshotRate = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (arg == "--set" && i + 2 < argc) {
      const std::string name = argv[i + 1];
      const std::string value = argv[i + 2];
      i += 2;
      if (auto r = Paper::cvars().set(name, value); !r) {
        spdlog::error("{}", r.error().message);
        return 1;
      }
    } else {
      usage();
      return arg == "--help" ? 0 : 1;
    }
  }
  if (config.snapshotRate == 0 || config.tickRate % config.snapshotRate != 0) {
    spdlog::error("snapshot rate must divide the tick rate ({})", config.tickRate);
    return 1;
  }

  auto map = Paper::loadMap(mapPath);
  if (!map) {
    spdlog::error("{}", map.error().message);
    return 1;
  }
  auto world = map->buildCollision();
  if (!world) {
    spdlog::error("{}", world.error().message);
    return 1;
  }
  Paper::MovementCvars movementCvars;
  config.movement = movementCvars.params();
  config.spawns = Paper::spawnPoints(*map);
  config.map = std::filesystem::path(mapPath).stem().string();

  auto transport = Paper::EnetTransport::createServer(port, Paper::kMaxPlayerSlots);
  if (!transport) {
    spdlog::error("{}", transport.error().message);
    return 1;
  }
  Paper::Server server(**transport, *world, config);
  spdlog::info("paper server: map '{}', {} Hz tick, {} Hz snapshots, port {}", config.map,
               config.tickRate, config.snapshotRate, (*transport)->port());

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  using Clock = std::chrono::steady_clock;
  Paper::FixedTimestep stepper(1.0 / config.tickRate, 16);
  auto last = Clock::now();
  auto nextReport = last + std::chrono::seconds(10);
  while (g_running) {
    const auto now = Clock::now();
    stepper.advance(std::chrono::duration<double>(now - last).count());
    last = now;
    while (stepper.consumeStep()) {
      server.tick();
    }
    if (now >= nextReport) {
      const auto &s = server.stats();
      spdlog::info("tick {}: {} players, {} starved, {} dropped, {} bad packets, {:.1f} KB/s out",
                   server.currentTick(), server.playerCount(), s.starvedTicks, s.cmdsDropped,
                   s.malformedPackets,
                   static_cast<double>(s.snapshotBytesSent) / 1024.0 /
                       (static_cast<double>(s.ticks) / config.tickRate));
      nextReport = now + std::chrono::seconds(10);
    }
    // Sleep most of the remaining step, then spin the last bit for accuracy.
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  spdlog::info("shutting down");
  return 0;
}
