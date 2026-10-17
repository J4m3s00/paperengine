#pragma once

#include "game/collision.h"
#include "game/movement.h"
#include "net/protocol.h"
#include "net/transport.h"

#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Paper {
struct SpawnPoint {
  glm::vec3 origin{0.0F};
  float yaw = 0.0F;
};

struct ServerConfig {
  std::uint16_t tickRate = 128;
  // Snapshots per second; tickRate must be a multiple of it.
  std::uint16_t snapshotRate = 64;
  std::string map = "dev_world";
  std::vector<SpawnPoint> spawns;
  MovementParams movement;
  // Usercmds a client may have queued; beyond this the oldest are dropped.
  std::size_t maxQueuedCmds = 16;
  // Ticks a client may run on its last input after its queue ran dry.
  int maxRepeatedCmds = 8;
};

struct ServerStats {
  std::uint64_t ticks = 0;
  std::uint64_t cmdsExecuted = 0;
  // Ticks where a client had no new usercmd.
  std::uint64_t starvedTicks = 0;
  std::uint64_t cmdsDropped = 0;
  std::uint64_t malformedPackets = 0;
  std::uint64_t snapshotsSent = 0;
  std::uint64_t snapshotBytesSent = 0;
  std::uint64_t fullSnapshotsSent = 0;
};

// The authoritative game server (ADR 0009). It owns no sockets or clocks: the
// caller passes a Transport and calls `tick()` at `tickRate`, so the same code
// runs in the dedicated server, in tests and in the bot harness.
class Server {
public:
  // Decides whether `viewer` may be told about `other` (anti-wallhack hook).
  // The default shows everyone.
  using VisibilityFn = std::function<bool(std::uint8_t viewer, std::uint8_t other)>;

  Server(Transport &transport, const CollisionWorld &world, ServerConfig config);

  void setVisibility(VisibilityFn fn) { m_visibility = std::move(fn); }
  // Handles incoming packets, runs one simulation tick, sends due snapshots.
  void tick();

  [[nodiscard]] std::uint32_t currentTick() const { return m_tick; }
  [[nodiscard]] const ServerStats &stats() const { return m_stats; }
  [[nodiscard]] int playerCount() const;
  // The authoritative state of a slot, or null if empty.
  [[nodiscard]] const PlayerState *playerState(std::uint8_t slot) const;
  // Usercmds waiting for execution (client clock sync reads this).
  [[nodiscard]] std::size_t queuedCmds(std::uint8_t slot) const;

private:
  struct Client {
    PeerId peer = 0;
    std::string name;
    PlayerState state;
    float yaw = 0.0F;
    float pitch = 0.0F;
    std::deque<NetUserCmd> queue;
    UserCmd lastCmd;
    int repeatedCmds = 0;
    // Highest usercmd sequence received / executed (0 = none yet).
    std::uint32_t newestSeq = 0;
    std::uint32_t executedSeq = 0;
    bool haveSeq = false;
    std::uint32_t ackedSnapshot = 0;
    std::deque<Snapshot> history;
  };

  void handlePacket(const NetEvent &event);
  void handleHello(PeerId peer, std::span<const std::uint8_t> packet);
  void handleUserCmds(Client &client, std::span<const std::uint8_t> packet);
  void removePeer(PeerId peer);
  void simulate(Client &client);
  void sendSnapshots();
  [[nodiscard]] Client *clientOfPeer(PeerId peer);

  Transport &m_transport;
  const CollisionWorld &m_world;
  ServerConfig m_config;
  VisibilityFn m_visibility;
  std::array<std::optional<Client>, kMaxPlayerSlots> m_clients;
  std::unordered_map<PeerId, std::uint8_t> m_slotOfPeer;
  std::uint32_t m_tick = 0;
  ServerStats m_stats;
};
} // namespace Paper
