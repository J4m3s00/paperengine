#include <doctest/doctest.h>

#include "net/client_session.h"
#include "net/loopback.h"
#include "net/server.h"

#include <cmath>

using namespace Paper;

namespace {
constexpr std::int64_t kTickNs = 1'000'000'000LL / 128;

const WeaponDb &weaponDb() {
  static const WeaponDb db = [] {
    auto d = WeaponDb::loadDirectory(PAPER_WEAPONS_DIR);
    REQUIRE(d.has_value());
    return std::move(*d);
  }();
  return db;
}

CollisionWorld makeWorld() {
  std::vector<BrushDesc> brushes{boxBrush({-3000, -100, -3000}, {3000, 0, 3000}),
                                 boxBrush({-3000, 0, -3000}, {3000, 200, -2990})};
  auto world = CollisionWorld::create(brushes);
  REQUIRE(world.has_value());
  return std::move(*world);
}

// A server, N clients and a virtual network, stepped one 128 Hz tick at a
// time (clients first, like inputs sampled before the server runs).
struct Rig {
  CollisionWorld world = makeWorld();
  LoopbackNetwork net;
  LoopbackNetwork::Endpoint &serverEp;
  Server server;
  std::vector<LoopbackNetwork::Endpoint *> clientEps;
  std::vector<std::unique_ptr<ClientSession>> clients;

  explicit Rig(std::uint64_t seed = 1, NetworkConditions cond = {})
      : net(seed), serverEp((net.setConditions(cond), net.addEndpoint())),
        server(serverEp, world, weaponDb(), makeConfig()) {}

  static ServerConfig makeConfig() {
    ServerConfig c;
    c.spawns = {{glm::vec3(0, 2, 0), 0.0F}};
    return c;
  }

  ClientSession &addClient() {
    auto &ep = net.addEndpoint();
    clientEps.push_back(&ep);
    clients.push_back(std::make_unique<ClientSession>(ep, serverEp.id(), world, weaponDb(),
                                                      MovementParams{}, "bot"));
    ep.connect(serverEp.id());
    return *clients.back();
  }

  template <typename InputFn> void run(int ticks, InputFn input) {
    for (int t = 0; t < ticks; ++t) {
      for (std::size_t i = 0; i < clients.size(); ++i) {
        clients[i]->tick(input(i, t));
      }
      server.tick();
      net.advance(kTickNs);
    }
  }
};

UserCmd runForward() {
  UserCmd c;
  c.forward = 1.0F;
  return c;
}
} // namespace

TEST_CASE("a client connects, is accepted and starts playing") {
  Rig rig;
  auto &client = rig.addClient();
  rig.run(20, [](auto, auto) { return UserCmd{}; });
  CHECK(client.status() == ClientSession::Status::Playing);
  CHECK(client.slot() == 0);
  CHECK(rig.server.playerCount() == 1);
}

TEST_CASE("the server rejects clients when full") {
  Rig rig;
  for (int i = 0; i < static_cast<int>(kMaxPlayerSlots) + 1; ++i) {
    rig.addClient();
  }
  rig.run(30, [](auto, auto) { return UserCmd{}; });
  CHECK(rig.server.playerCount() == static_cast<int>(kMaxPlayerSlots));
  CHECK(rig.clients.back()->status() == ClientSession::Status::Rejected);
  CHECK(rig.clients.back()->rejectReason() == "server full");
}

TEST_CASE("prediction matches the server on a perfect network") {
  Rig rig;
  auto &client = rig.addClient();
  rig.run(256, [](auto, auto) { return runForward(); });
  CHECK(client.stats().corrections == 0);
  CHECK(client.stats().snapshotsReceived > 50);
  const PlayerState *server = rig.server.playerState(0);
  REQUIRE(server != nullptr);
  CHECK(server->origin.z < -100.0F);
  // The client is ahead of the server by about the in-flight cmds.
  CHECK(client.predicted().origin.z <= server->origin.z + 0.01F);
}

TEST_CASE("prediction stays correct under latency, jitter and loss") {
  Rig rig(7, {.latencyMs = 50, .jitterMs = 8, .loss = 0.02});
  auto &client = rig.addClient();
  rig.run(128 * 20, [](auto, auto t) {
    UserCmd c = runForward();
    c.side = (t / 64) % 2 == 0 ? 1.0F : -1.0F;
    c.yaw = 0.002F * static_cast<float>(t);
    c.jump = t % 200 == 0;
    return c;
  });
  CHECK(client.playing());
  MESSAGE("corrections " << client.stats().corrections << " of " << client.stats().snapshotsReceived
          << " snapshots, starved ticks " << rig.server.stats().starvedTicks << ", input RTT "
          << client.stats().inputRttMs << " ms");
  // Corrections only come from starved server ticks (late/lost cmds), and
  // must be a small share of all snapshots.
  CHECK(client.stats().corrections * 20 < client.stats().snapshotsReceived);
  const PlayerState *server = rig.server.playerState(0);
  REQUIRE(server != nullptr);
  CHECK(rig.server.stats().cmdsExecuted > 128 * 18);
}

TEST_CASE("the client's time scale keeps the server queue small") {
  Rig rig(3, {.latencyMs = 40, .jitterMs = 4});
  auto &client = rig.addClient();
  rig.run(128 * 10, [](auto, auto) { return runForward(); });
  CHECK(rig.server.queuedCmds(0) <= 6);
  CHECK(client.timeScale() > 0.95);
  CHECK(client.timeScale() < 1.06);
}

TEST_CASE("remote players are interpolated from snapshots") {
  Rig rig;
  auto &a = rig.addClient();
  rig.addClient();
  rig.run(300, [](std::size_t who, auto) { return who == 1 ? runForward() : UserCmd{}; });
  const auto remotes = a.remotePlayers();
  REQUIRE(remotes.size() == 1);
  CHECK(remotes[0].slot == 1);
  const PlayerState *truth = rig.server.playerState(1);
  REQUIRE(truth != nullptr);
  // Drawn in the past, so behind the real position, but close.
  CHECK(remotes[0].state.origin.z > truth->origin.z);
  CHECK(remotes[0].state.origin.z < truth->origin.z + 60.0F);
}

TEST_CASE("visibility hook hides players from a client") {
  Rig rig;
  auto &a = rig.addClient();
  rig.addClient();
  rig.server.setVisibility([](std::uint8_t, std::uint8_t) { return false; });
  rig.run(100, [](auto, auto) { return UserCmd{}; });
  CHECK(a.remotePlayers().empty());
  CHECK(a.playing());
}

TEST_CASE("a speed hack cannot execute more than one usercmd per tick") {
  Rig rig;
  auto &ep = rig.net.addEndpoint();
  ep.connect(rig.serverEp.id());
  rig.net.advance(kTickNs);
  ep.send(rig.serverEp.id(), Channel::Reliable, encode(HelloMsg{kProtocolVersion, "cheater"}));
  rig.net.advance(kTickNs);
  rig.server.tick();
  // Flood 1000 cmds in a burst.
  for (std::uint32_t seq = 1; seq <= 1000; seq += 8) {
    UserCmdsMsg msg;
    for (std::uint32_t i = 0; i < 8; ++i) {
      UserCmd c;
      c.forward = 1.0F;
      msg.cmds.push_back({seq + i, c});
    }
    ep.send(rig.serverEp.id(), Channel::Unreliable, encode(msg));
  }
  rig.net.advance(kTickNs);
  const auto before = rig.server.stats().cmdsExecuted;
  for (int i = 0; i < 10; ++i) {
    rig.server.tick();
  }
  CHECK(rig.server.stats().cmdsExecuted - before <= 10);
  CHECK(rig.server.stats().cmdsDropped > 0);
}

TEST_CASE("a client leaving frees its slot") {
  Rig rig;
  auto &client = rig.addClient();
  rig.run(30, [](auto, auto) { return UserCmd{}; });
  REQUIRE(rig.server.playerCount() == 1);
  rig.clientEps[0]->disconnect(rig.serverEp.id());
  rig.run(5, [](auto, auto) { return UserCmd{}; });
  CHECK(rig.server.playerCount() == 0);
  CHECK(client.status() == ClientSession::Status::Disconnected);
}

namespace {
// Looks along -Z (yaw 0) with the given pitch while holding the trigger.
UserCmd aimAndFire(float pitch) {
  UserCmd c;
  c.pitch = pitch;
  c.attack = true;
  return c;
}
} // namespace

TEST_CASE("a headshot from the AK kills a stationary bot, which respawns later") {
  Rig rig;
  auto &client = rig.addClient();
  const int bot = rig.server.addBot({glm::vec3(0, 2, -300), 0.0F});
  REQUIRE(bot >= 0);
  rig.run(40, [](auto, auto) { return UserCmd{}; }); // settle, join
  REQUIRE(rig.server.vitals(static_cast<std::uint8_t>(bot))->alive);

  rig.run(6, [](auto, auto) { return aimAndFire(0.0F); }); // eye height hits the head
  rig.run(20, [](auto, auto) { return UserCmd{}; });
  const Vitals *v = rig.server.vitals(static_cast<std::uint8_t>(bot));
  CHECK_FALSE(v->alive);
  CHECK(rig.server.stats().kills == 1);

  bool sawHit = false;
  bool sawKill = false;
  for (const GameEvent &e : client.takeEvents()) {
    if (e.type == GameEvent::Type::Hit) {
      sawHit = true;
      CHECK(e.headshot);
      CHECK(e.target == bot);
    }
    sawKill = sawKill || (e.type == GameEvent::Type::Kill && e.target == bot);
  }
  CHECK(sawHit);
  CHECK(sawKill);

  rig.run(128 * 4, [](auto, auto) { return UserCmd{}; });
  CHECK(rig.server.vitals(static_cast<std::uint8_t>(bot))->alive);
  CHECK(rig.server.vitals(static_cast<std::uint8_t>(bot))->health == 100);
}

TEST_CASE("body shots take several bullets, armor absorbs part of them") {
  Rig rig;
  rig.addClient();
  const int bot = rig.server.addBot({glm::vec3(0, 2, -300), 0.0F});
  rig.run(40, [](auto, auto) { return UserCmd{}; });
  // Aim at the chest: 12 units below the eye at 300 units.
  const float pitch = -std::atan(12.0F / 300.0F);
  rig.run(2, [&](auto, auto) { return aimAndFire(pitch); });
  rig.run(3, [](auto, auto) { return UserCmd{}; });
  const Vitals *v = rig.server.vitals(static_cast<std::uint8_t>(bot));
  CHECK(v->alive);
  CHECK(v->health < 100);
  CHECK(v->health > 60);
  CHECK(v->armor < 100);
}

TEST_CASE("walls block bullets") {
  Rig rig;
  rig.addClient();
  // Rebuild the world with a wall between shooter and bot.
  std::vector<BrushDesc> brushes{boxBrush({-3000, -100, -3000}, {3000, 0, 3000}),
                                 boxBrush({-50, 0, -150}, {50, 200, -140})};
  auto walled = CollisionWorld::create(brushes);
  REQUIRE(walled.has_value());
  LoopbackNetwork net;
  auto &serverEp = net.addEndpoint();
  auto &clientEp = net.addEndpoint();
  Server server(serverEp, *walled, weaponDb(), Rig::makeConfig());
  ClientSession client(clientEp, serverEp.id(), *walled, weaponDb(), MovementParams{}, "c");
  clientEp.connect(serverEp.id());
  const int bot = server.addBot({glm::vec3(0, 2, -300), 0.0F});
  for (int t = 0; t < 60; ++t) {
    client.tick(t < 40 ? UserCmd{} : aimAndFire(0.0F));
    server.tick();
    net.advance(kTickNs);
  }
  CHECK(server.vitals(static_cast<std::uint8_t>(bot))->health == 100);
  CHECK(server.stats().shotsFired > 0);
  CHECK(server.stats().hits == 0);
}

TEST_CASE("other players see shots as events, the shooter predicts its own") {
  Rig rig;
  auto &shooter = rig.addClient();
  auto &watcher = rig.addClient();
  rig.run(40, [](auto, auto) { return UserCmd{}; });
  rig.run(1, [](std::size_t who, auto) { return who == 0 ? aimAndFire(0.5F) : UserCmd{}; });
  rig.run(10, [](auto, auto) { return UserCmd{}; });
  CHECK(shooter.takeLocalShots().size() == 1);
  int shotEvents = 0;
  for (const GameEvent &e : watcher.takeEvents()) {
    shotEvents += e.type == GameEvent::Type::Shot ? 1 : 0;
  }
  CHECK(shotEvents == 1);
  for (const GameEvent &e : shooter.takeEvents()) {
    CHECK(e.type != GameEvent::Type::Shot); // not echoed back to the shooter
  }
}

TEST_CASE("predicted ammo and recoil match the server while spraying under bad network") {
  Rig rig(5, {.latencyMs = 50, .jitterMs = 6, .loss = 0.02});
  auto &client = rig.addClient();
  rig.run(128 * 20, [](auto, auto t) {
    UserCmd c;
    c.attack = (t / 64) % 2 == 0;
    c.reload = t % 640 == 639;
    c.forward = (t / 100) % 2 == 0 ? 1.0F : 0.0F;
    return c;
  });
  CHECK(client.stats().corrections * 15 < client.stats().snapshotsReceived);
  const CombatState *server = rig.server.combat(0);
  REQUIRE(server != nullptr);
  CHECK(client.combat().weapon == server->weapon);
  CHECK(rig.server.stats().shotsFired > 50);
}

TEST_CASE("weapon switching and reloading are predicted") {
  Rig rig;
  auto &client = rig.addClient();
  rig.run(30, [](auto, auto) { return UserCmd{}; });
  rig.run(1, [](auto, auto) {
    UserCmd c;
    c.weaponSelect = 4; // Glock
    return c;
  });
  CHECK(client.combat().weapon == 3); // immediately, before the server answers
  rig.run(40, [](auto, auto) { return UserCmd{}; });
  CHECK(rig.server.combat(0)->weapon == 3);
  CHECK(client.stats().corrections == 0);
}
