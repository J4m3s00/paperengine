#pragma once

#include "game/collision.h"
#include "game/movement.h"
#include "net/protocol.h"
#include "net/transport.h"

#include <deque>
#include <string>
#include <vector>

namespace Paper {
struct ClientConfig {
  std::uint16_t tickRate = 128;
  // How far behind the newest snapshot remote players are drawn.
  double interpolationDelayMs = 50.0;
  // Usercmds the server should keep queued as a jitter buffer.
  int targetCmdQueue = 2;
  // A correction larger than this (units) snaps instead of smoothing.
  float snapDistance = 64.0F;
  // Seconds for a smoothed correction to decay to 1/e.
  float correctionDecay = 0.1F;
};

struct ClientStats {
  std::uint64_t snapshotsReceived = 0;
  std::uint64_t cmdsSent = 0;
  std::uint64_t bytesSent = 0;
  std::uint64_t bytesReceived = 0;
  // Reconciliations where the server disagreed with the prediction.
  std::uint64_t corrections = 0;
  // Size of the latest correction in units.
  float lastCorrection = 0.0F;
  // Time from sending a usercmd to seeing the server acknowledge it.
  double inputRttMs = 0.0;
  // Age of the newest snapshot in ms (grows when snapshots stop arriving).
  double snapshotAgeMs = 0.0;
};

// The client side of the netcode (ADR 0009), independent of rendering and
// sockets: feed it one input per tick, read back the predicted local player
// and interpolated remote players. Prediction uses the shared `movePlayer`.
class ClientSession {
public:
  enum class Status { Connecting, Joining, Playing, Rejected, Disconnected };

  ClientSession(Transport &transport, PeerId server, const CollisionWorld &world,
                MovementParams params, std::string name, ClientConfig config = {});

  // One client tick: receive packets, reconcile, predict this input, send.
  void tick(const UserCmd &input);

  [[nodiscard]] Status status() const { return m_status; }
  [[nodiscard]] const std::string &rejectReason() const { return m_rejectReason; }
  [[nodiscard]] std::uint8_t slot() const { return m_slot; }
  // True once the first snapshot arrived and prediction runs.
  [[nodiscard]] bool playing() const { return m_status == Status::Playing; }

  // The locally predicted state (what the next usercmd builds on).
  [[nodiscard]] const PlayerState &predicted() const { return m_predicted; }
  // Predicted feet position plus the fading correction offset: draw this.
  [[nodiscard]] glm::vec3 visualOrigin() const { return m_predicted.origin + m_errorOffset; }
  // Other players, interpolated to the current render time.
  [[nodiscard]] std::vector<NetPlayer> remotePlayers() const;
  // Multiply the tick rate by this to keep the server's usercmd queue near
  // its target (about 1; changes by a few percent at most).
  [[nodiscard]] double timeScale() const { return m_timeScale; }
  [[nodiscard]] const ClientStats &stats() const { return m_stats; }

private:
  struct Pending {
    std::uint32_t seq;
    UserCmd cmd;
    std::int64_t sentTick;
    PlayerState after; // predicted state after executing it
  };

  void receive();
  void onAccept(std::span<const std::uint8_t> packet);
  void onSnapshot(std::span<const std::uint8_t> packet, std::size_t size);
  void reconcile(const Snapshot &snapshot, const NetPlayer &self);
  void sendCmds();
  [[nodiscard]] double dt() const { return 1.0 / m_config.tickRate; }

  Transport &m_transport;
  PeerId m_server;
  const CollisionWorld &m_world;
  MovementParams m_params;
  std::string m_name;
  ClientConfig m_config;

  Status m_status = Status::Connecting;
  std::string m_rejectReason;
  std::uint8_t m_slot = 0;
  std::int64_t m_localTick = 0;

  PlayerState m_predicted;
  glm::vec3 m_errorOffset{0.0F};
  std::deque<Pending> m_pending;
  std::uint32_t m_nextSeq = 1;

  std::deque<Snapshot> m_snapshots;
  std::uint32_t m_newestSnapshotTick = 0;
  std::int64_t m_lastSnapshotLocalTick = 0;
  // Smoothed (server time - local time) in seconds.
  double m_clockOffset = 0.0;
  bool m_haveClock = false;

  double m_timeScale = 1.0;
  ClientStats m_stats;
};
} // namespace Paper
