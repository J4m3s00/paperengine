#include <doctest/doctest.h>

#include "net/client_session.h"
#include "net/loopback.h"
#include "net/server.h"

#include <cmath>

using namespace Paper;

namespace {
constexpr std::int64_t kTickNs = 1'000'000'000LL / 128;

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
        server(serverEp, world, makeConfig()) {}

  static ServerConfig makeConfig() {
    ServerConfig c;
    c.spawns = {{glm::vec3(0, 2, 0), 0.0F}};
    return c;
  }

  ClientSession &addClient() {
    auto &ep = net.addEndpoint();
    clientEps.push_back(&ep);
    clients.push_back(std::make_unique<ClientSession>(ep, serverEp.id(), world, MovementParams{},
                                                      "bot"));
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
