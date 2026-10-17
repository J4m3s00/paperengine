// Bot clients against a server, for soak tests and perf numbers.
//
//   bots --inprocess [--bots N] [--seconds S] [--latency ms] [--jitter ms] [--loss 0.02]
//       Server and bots share one process and a virtual clock (runs much
//       faster than real time; exercises the network conditioner).
//   bots --inprocess --teams ...   team elimination with short rounds
//   bots --connect host[:port] [--bots N] [--seconds S]
//       Real-time bots over UDP against a running server.
#include "game/map.h"
#include "game/weapon_def.h"
#include "net/bot.h"
#include "net/client_session.h"
#include "net/enet_transport.h"
#include "net/loopback.h"
#include "net/server.h"

#include "base/paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

using namespace Paper;
using Clock = std::chrono::steady_clock;

namespace {
struct Options {
  bool inprocess = false;
  std::string host = "127.0.0.1";
  std::uint16_t port = kDefaultServerPort;
  int bots = 10;
  double seconds = 30.0;
  NetworkConditions conditions;
  std::uint64_t seed = 1;
  bool teams = false;
  std::string map = (Paper::dataDir("maps", PAPER_MAPS_DIR) / "dev_world.json").string();
};

void report(const std::vector<std::unique_ptr<ClientSession>> &clients, double seconds) {
  std::uint64_t corrections = 0;
  std::uint64_t snapshots = 0;
  std::uint64_t bytesIn = 0;
  std::uint64_t bytesOut = 0;
  float worst = 0.0F;
  int playing = 0;
  for (const auto &c : clients) {
    const ClientStats &s = c->stats();
    corrections += s.corrections;
    snapshots += s.snapshotsReceived;
    bytesIn += s.bytesReceived;
    bytesOut += s.bytesSent;
    worst = std::max(worst, s.lastCorrection);
    playing += c->playing() ? 1 : 0;
  }
  const double n = static_cast<double>(std::max<std::size_t>(clients.size(), 1));
  spdlog::info("bots: {}/{} playing, {} corrections in {} snapshots ({:.3f}%), last correction max {:.2f}u",
               playing, clients.size(), corrections, snapshots,
               snapshots ? 100.0 * static_cast<double>(corrections) / static_cast<double>(snapshots) : 0.0,
               worst);
  spdlog::info("bandwidth per client: {:.1f} KB/s down, {:.1f} KB/s up",
               static_cast<double>(bytesIn) / n / seconds / 1024.0,
               static_cast<double>(bytesOut) / n / seconds / 1024.0);
}

int runInProcess(const Options &opt, const CollisionWorld &world, const MapData &map,
                 const WeaponDb &weapons) {
  LoopbackNetwork net(opt.seed);
  net.setConditions(opt.conditions);
  auto &serverEp = net.addEndpoint();
  ServerConfig config;
  config.spawns = spawnPoints(map);
  if (opt.teams) {
    // Short rounds so a soak sees many of them.
    config.teamplay = true;
    config.rules.freezeSeconds = 2.0F;
    config.rules.roundSeconds = 30.0F;
    config.rules.roundEndSeconds = 2.0F;
    config.rules.matchEndSeconds = 3.0F;
    config.rules.roundsToWin = 3;
    config.spawnsT = teamSpawnPoints(map, Team::T);
    config.spawnsCT = teamSpawnPoints(map, Team::CT);
  }
  Server server(serverEp, world, weapons, config);

  std::vector<std::unique_ptr<ClientSession>> clients;
  std::vector<BotBrain> brains;
  for (int i = 0; i < opt.bots; ++i) {
    auto &ep = net.addEndpoint();
    clients.push_back(std::make_unique<ClientSession>(ep, serverEp.id(), world, weapons, config.movement,
                                                      "bot" + std::to_string(i)));
    brains.emplace_back(opt.seed * 1000 + static_cast<std::uint64_t>(i), true);
    ep.connect(serverEp.id());
  }

  const auto totalTicks = static_cast<std::int64_t>(opt.seconds * config.tickRate);
  constexpr std::int64_t kTickNs = 1'000'000'000LL / 128;
  double serverMicros = 0.0;
  double worstMicros = 0.0;
  std::uint64_t prevCorrections = 0;
  const auto wallStart = Clock::now();
  for (std::int64_t t = 0; t < totalTicks; ++t) {
    for (std::size_t i = 0; i < clients.size(); ++i) {
      clients[i]->tick(brains[i].next());
    }
    const auto a = Clock::now();
    server.tick();
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - a).count();
    serverMicros += us;
    worstMicros = std::max(worstMicros, us);
    net.advance(kTickNs);

    // Corrections per simulated minute: should stay flat, not grow.
    if ((t + 1) % (60 * config.tickRate) == 0 || t + 1 == totalTicks) {
      std::uint64_t corrections = 0;
      for (const auto &c : clients) corrections += c->stats().corrections;
      spdlog::info("t={:.0f}s: +{} corrections, {} players", static_cast<double>(t + 1) / config.tickRate,
                   corrections - prevCorrections, server.playerCount());
      prevCorrections = corrections;
    }
  }
  const double wall = std::chrono::duration<double>(Clock::now() - wallStart).count();
  const auto &s = server.stats();
  spdlog::info("simulated {:.0f}s in {:.1f}s wall; server tick avg {:.1f}us worst {:.1f}us (budget 7812us)",
               opt.seconds, wall, serverMicros / static_cast<double>(s.ticks), worstMicros);
  spdlog::info("server: {} cmds executed, {} starved ticks, {} cmds dropped, {} full / {} snapshots, avg snapshot {:.0f} bytes",
               s.cmdsExecuted, s.starvedTicks, s.cmdsDropped, s.fullSnapshotsSent, s.snapshotsSent,
               s.snapshotsSent ? static_cast<double>(s.snapshotBytesSent) / static_cast<double>(s.snapshotsSent) : 0.0);
  spdlog::info("network: {} packets, {} dropped", net.packetsSent(), net.packetsDropped());
  if (opt.teams) {
    const MatchState m = server.matchState();
    int agree = 0;
    for (const auto &c : clients) {
      agree += c->match().phase == m.phase && c->match().round == m.round &&
                       c->match().scoreT == m.scoreT && c->match().scoreCT == m.scoreCT
                   ? 1
                   : 0;
    }
    spdlog::info("rounds played {}, now round {} ({}:{}), {}/{} clients agree with the server on the match state",
                 s.roundsPlayed, m.round, m.scoreT, m.scoreCT, agree, clients.size());
  }
  report(clients, opt.seconds);
  return 0;
}

int runRealtime(const Options &opt, const CollisionWorld &world, const WeaponDb &weapons) {
  struct Bot {
    std::unique_ptr<EnetTransport> transport;
    std::unique_ptr<ClientSession> session;
    BotBrain brain;
  };
  std::vector<Bot> bots;
  MovementCvars cvarsHolder;
  for (int i = 0; i < opt.bots; ++i) {
    auto transport = EnetTransport::createClient();
    if (!transport) {
      spdlog::error("{}", transport.error().message);
      return 1;
    }
    auto peer = (*transport)->connect(opt.host, opt.port);
    if (!peer) {
      spdlog::error("{}", peer.error().message);
      return 1;
    }
    auto session = std::make_unique<ClientSession>(**transport, *peer, world, weapons, cvarsHolder.params(),
                                                   "bot" + std::to_string(i));
    bots.push_back({std::move(*transport), std::move(session),
                    BotBrain(opt.seed * 1000 + static_cast<std::uint64_t>(i), true)});
  }
  const auto start = Clock::now();
  auto next = start;
  std::vector<std::unique_ptr<ClientSession>> sessions;
  while (std::chrono::duration<double>(Clock::now() - start).count() < opt.seconds) {
    for (auto &bot : bots) {
      bot.session->tick(bot.brain.next());
    }
    next += std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / 128.0));
    std::this_thread::sleep_until(next);
  }
  for (auto &bot : bots) {
    sessions.push_back(std::move(bot.session));
  }
  report(sessions, opt.seconds);
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--inprocess") {
      opt.inprocess = true;
    } else if (arg == "--connect" && hasValue) {
      const std::string target = argv[++i];
      const auto colon = target.find(':');
      opt.host = target.substr(0, colon);
      if (colon != std::string::npos) opt.port = static_cast<std::uint16_t>(std::atoi(target.c_str() + colon + 1));
    } else if (arg == "--bots" && hasValue) {
      opt.bots = std::atoi(argv[++i]);
    } else if (arg == "--seconds" && hasValue) {
      opt.seconds = std::atof(argv[++i]);
    } else if (arg == "--latency" && hasValue) {
      opt.conditions.latencyMs = std::atof(argv[++i]);
    } else if (arg == "--jitter" && hasValue) {
      opt.conditions.jitterMs = std::atof(argv[++i]);
    } else if (arg == "--loss" && hasValue) {
      opt.conditions.loss = std::atof(argv[++i]);
    } else if (arg == "--seed" && hasValue) {
      opt.seed = static_cast<std::uint64_t>(std::atoll(argv[++i]));
    } else if (arg == "--teams") {
      opt.teams = true;
    } else if (arg == "--map" && hasValue) {
      opt.map = argv[++i];
    } else {
      spdlog::error("unknown or incomplete argument '{}'", arg);
      return 1;
    }
  }
  auto map = loadMap(opt.map);
  if (!map) {
    spdlog::error("{}", map.error().message);
    return 1;
  }
  auto world = map->buildCollision();
  if (!world) {
    spdlog::error("{}", world.error().message);
    return 1;
  }
  auto weapons = WeaponDb::loadDirectory(Paper::dataDir("weapons", PAPER_WEAPONS_DIR));
  if (!weapons) {
    spdlog::error("{}", weapons.error().message);
    return 1;
  }
  return opt.inprocess ? runInProcess(opt, *world, *map, *weapons) : runRealtime(opt, *world, *weapons);
}
