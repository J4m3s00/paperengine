#pragma once

#include "game/collision.h"
#include "game/movement.h"
#include "game/phase_input.h"
#include "game/weapon.h"
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
                const WeaponDb &weapons, MovementParams params, std::string name,
                ClientConfig config = {});

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

  // Own weapon state (predicted) and health (from the server).
  [[nodiscard]] const CombatState &combat() const { return m_combat; }
  [[nodiscard]] const Vitals &vitals() const { return m_vitals; }
  [[nodiscard]] const WeaponDb &weapons() const { return m_weapons; }
  [[nodiscard]] const MatchState &match() const { return m_match; }
  // Everyone in the newest snapshot (not interpolated): scoreboard data.
  [[nodiscard]] const std::vector<NetPlayer> &latestPlayers() const;
  // Server ticks until the current phase ends, by the client's clock estimate.
  [[nodiscard]] double phaseSecondsLeft() const;
  // Shots the local player fired since the last call (predicted, for effects).
  [[nodiscard]] std::vector<ShotInfo> takeLocalShots();
  // Server events (other players' shots, hits on/by us, kills) since the last call.
  [[nodiscard]] std::vector<GameEvent> takeEvents();

private:
  struct Pending {
    std::uint32_t seq;
    UserCmd cmd;
    std::int64_t sentTick;
    PlayerState after; // predicted state after executing it
    CombatState combatAfter;
  };

  void receive();
  void onEvent(std::span<const std::uint8_t> packet);
  void onAccept(std::span<const std::uint8_t> packet);
  void onSnapshot(std::span<const std::uint8_t> packet, std::size_t size);
  void reconcile(const Snapshot &snapshot, const NetPlayer &self);
  void sendCmds();
  [[nodiscard]] double dt() const { return 1.0 / m_config.tickRate; }

  Transport &m_transport;
  PeerId m_server;
  const CollisionWorld &m_world;
  const WeaponDb &m_weapons;
  MovementParams m_params;
  std::string m_name;
  ClientConfig m_config;

  Status m_status = Status::Connecting;
  std::string m_rejectReason;
  std::uint8_t m_slot = 0;
  std::int64_t m_localTick = 0;

  PlayerState m_predicted;
  CombatState m_combat;
  Vitals m_vitals;
  MatchState m_match;
  std::vector<ShotInfo> m_localShots;
  std::vector<GameEvent> m_events;
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
