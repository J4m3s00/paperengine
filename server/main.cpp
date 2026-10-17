#include "base/cvar.h"
#include "base/paths.h"
#include "base/fixed_timestep.h"
#include "game/map.h"
#include "game/weapon_def.h"
#include "net/enet_transport.h"
#include "net/server.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <expected>
#include <filesystem>
#include <memory>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

void usage() {
  spdlog::info("usage: server [--map name|file.json] [--maps a,b,c] [--port N] [--snapshot-rate N] [--weapons dir] [--moving-bots] [--config file.cfg] [--set cvar value]");
}
} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> maps;
  std::uint16_t port = Paper::kDefaultServerPort;
  Paper::ServerConfig config;
  std::string weaponsDir = Paper::dataDir("weapons", PAPER_WEAPONS_DIR).string();
  bool movingBots = false;
  std::string configPath;
  std::vector<std::pair<std::string, std::string>> overrides;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--map" && hasValue) {
      maps = {argv[++i]};
    } else if (arg == "--maps" && hasValue) {
      // Comma separated rotation: the next map starts after each match.
      maps.clear();
      std::string list = argv[++i];
      for (std::size_t from = 0; from <= list.size();) {
        const auto comma = list.find(',', from);
        const auto end = comma == std::string::npos ? list.size() : comma;
        if (end > from) maps.push_back(list.substr(from, end - from));
        from = end + 1;
      }
    } else if (arg == "--port" && hasValue) {
      port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (arg == "--snapshot-rate" && hasValue) {
      config.snapshotRate = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (arg == "--weapons" && hasValue) {
      weaponsDir = argv[++i];
    } else if (arg == "--config" && hasValue) {
      configPath = argv[++i];
    } else if (arg == "--moving-bots") {
      movingBots = true;
    } else if (arg == "--set" && i + 2 < argc) {
      overrides.emplace_back(argv[i + 1], argv[i + 2]);
      i += 2;
    } else {
      usage();
      return arg == "--help" ? 0 : 1;
    }
  }
  if (config.snapshotRate == 0 || config.tickRate % config.snapshotRate != 0) {
    spdlog::error("snapshot rate must divide the tick rate ({})", config.tickRate);
    return 1;
  }

  auto weapons = Paper::WeaponDb::loadDirectory(weaponsDir);
  if (!weapons) {
    spdlog::error("{}", weapons.error().message);
    return 1;
  }
  Paper::MovementCvars movementCvars;
  Paper::RulesCvars rulesCvars;
  if (!configPath.empty()) {
    if (auto r = Paper::cvars().loadFile(configPath); !r) {
      spdlog::error("{}", r.error().message);
      return 1;
    }
  }
  for (const auto &[name, value] : overrides) {
    if (auto r = Paper::cvars().set(name, value); !r) {
      spdlog::error("{}", r.error().message);
      return 1;
    }
  }
  config.movement = movementCvars.params();
  config.teamplay = rulesCvars.teamplay();
  config.rules = rulesCvars.rules();
  config.friendlyFire = rulesCvars.friendlyFire();
  config.respawnSeconds = rulesCvars.respawnSeconds();

  auto transport = Paper::EnetTransport::createServer(port, Paper::kMaxPlayerSlots);
  if (!transport) {
    spdlog::error("{}", transport.error().message);
    return 1;
  }

  // The loaded map and the Server running it; replaced on a map change.
  struct Game {
    Paper::MapData map;
    std::unique_ptr<Paper::CollisionWorld> world;
    std::unique_ptr<Paper::Server> server;
  };
  const auto startGame = [&](const std::string &name) -> std::expected<Game, Paper::Error> {
    const std::filesystem::path path(name);
    auto map = Paper::loadMap(path.has_parent_path() || path.extension() == ".json"
                                  ? path
                                  : Paper::dataDir("maps", PAPER_MAPS_DIR) / (name + ".json"));
    if (!map) {
      return std::unexpected(map.error());
    }
    auto collision = map->buildCollision();
    if (!collision) {
      return std::unexpected(collision.error());
    }
    Game game;
    game.map = std::move(*map);
    game.world = std::make_unique<Paper::CollisionWorld>(std::move(*collision));
    Paper::ServerConfig cfg = config;
    cfg.spawns = Paper::spawnPoints(game.map);
    cfg.spawnsT = Paper::teamSpawnPoints(game.map, Paper::Team::T);
    cfg.spawnsCT = Paper::teamSpawnPoints(game.map, Paper::Team::CT);
    cfg.map = path.stem().string();
    cfg.mapChecksum = Paper::mapChecksum(game.map);
    game.server = std::make_unique<Paper::Server>(**transport, *game.world, *weapons, cfg);
    // Practice targets only in free-for-all; team matches are for players.
    if (!cfg.teamplay) {
      for (const Paper::MapEntity &bot : game.map.entitiesOfType("bot")) {
        game.server->addBot({bot.origin, bot.yaw}, movingBots);
      }
    }
    return game;
  };
  if (maps.empty()) {
    maps.push_back("dev_world");
  }
  std::size_t mapIndex = 0;
  auto first = startGame(maps[0]);
  if (!first) {
    spdlog::error("{}", first.error().message);
    return 1;
  }
  Game game = std::move(*first);
  spdlog::info("paper server: map '{}', {} Hz tick, {} Hz snapshots, port {}, {}", maps[0],
               config.tickRate, config.snapshotRate, (*transport)->port(),
               config.teamplay ? "team elimination" : "free for all");

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
      game.server->tick();
    }
    if (game.server->takeMatchRestarted() && maps.size() > 1) {
      // Rotate: send everyone away (their clients reconnect and load the new
      // map), let the goodbyes go out, then start the next map.
      mapIndex = (mapIndex + 1) % maps.size();
      spdlog::info("match over, changing map to '{}'", maps[mapIndex]);
      game.server->kickAll("map change");
      for (int i = 0; i < 20; ++i) {
        Paper::NetEvent flush;
        while ((*transport)->poll(flush)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (auto next = startGame(maps[mapIndex])) {
        game = std::move(*next);
      } else {
        spdlog::error("{} (staying on the current map)", next.error().message);
      }
    }
    if (now >= nextReport) {
      const auto &s = game.server->stats();
      spdlog::info("tick {}: {} players, {} starved, {} dropped, {} bad packets, {:.1f} KB/s out",
                   game.server->currentTick(), game.server->playerCount(), s.starvedTicks, s.cmdsDropped,
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
