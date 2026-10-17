#pragma once

#include "game/collision.h"
#include "game/hitbox.h"
#include "game/lag_compensation.h"
#include "game/map.h"
#include "game/rules.h"
#include "game/movement.h"
#include "game/phase_input.h"
#include "game/weapon.h"
#include "net/bot.h"
#include "net/protocol.h"
#include "net/transport.h"

#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace Paper {
struct SpawnPoint {
  glm::vec3 origin{0.0F};
  float yaw = 0.0F;
};

// The "spawn" entities of a map.
[[nodiscard]] std::vector<SpawnPoint> spawnPoints(const MapData &map);
// Spawns of a team: "spawn_t" / "spawn_ct" entities, or every "spawn" if the
// map has no team spawns.
[[nodiscard]] std::vector<SpawnPoint> teamSpawnPoints(const MapData &map, Team team);

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
  // How far back shots may be rewound (ADR 0009), like sv_maxunlag.
  float maxRewindSeconds = 0.2F;
  float respawnSeconds = 3.0F;
  // Team elimination rounds (game/rules.h). Off: free-for-all, everyone
  // respawns after respawnSeconds.
  bool teamplay = false;
  RulesConfig rules;
  bool friendlyFire = false;
  std::vector<SpawnPoint> spawnsT;
  std::vector<SpawnPoint> spawnsCT;
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
  std::uint64_t shotsFired = 0;
  std::uint64_t hits = 0;
  std::uint64_t kills = 0;
  std::uint64_t roundsPlayed = 0;
};

// The authoritative game server (ADR 0009). It owns no sockets or clocks: the
// caller passes a Transport and calls `tick()` at `tickRate`, so the same code
// runs in the dedicated server, in tests and in the bot harness.
class Server {
public:
  // Decides whether `viewer` may be told about `other` (anti-wallhack hook).
  // The default shows everyone.
  using VisibilityFn = std::function<bool(std::uint8_t viewer, std::uint8_t other)>;

  Server(Transport &transport, const CollisionWorld &world, const WeaponDb &weapons,
         ServerConfig config);

  // A server-controlled player (no connection): stands still, or walks about
  // with scripted input. Returns its slot or -1 if full.
  int addBot(const SpawnPoint &spawn, bool moving = false);

  void setVisibility(VisibilityFn fn) { m_visibility = std::move(fn); }
  // Tells every player to leave (they get `reason`), e.g. before a map change.
  void kickAll(const std::string &reason);
  // True once after a finished match started over (the time to rotate maps).
  [[nodiscard]] bool takeMatchRestarted() { return std::exchange(m_matchRestarted, false); }
  // Handles incoming packets, runs one simulation tick, sends due snapshots.
  void tick();

  [[nodiscard]] std::uint32_t currentTick() const { return m_tick; }
  [[nodiscard]] const ServerStats &stats() const { return m_stats; }
  [[nodiscard]] int playerCount() const;
  // The authoritative state of a slot, or null if empty.
  [[nodiscard]] const PlayerState *playerState(std::uint8_t slot) const;
  // Usercmds waiting for execution (client clock sync reads this).
  [[nodiscard]] std::size_t queuedCmds(std::uint8_t slot) const;
  [[nodiscard]] const Vitals *vitals(std::uint8_t slot) const;
  [[nodiscard]] const CombatState *combat(std::uint8_t slot) const;
  [[nodiscard]] const PlayerInfo *info(std::uint8_t slot) const;
  [[nodiscard]] MatchState matchState() const { return m_rules.matchState(); }

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
    CombatState combat;
    Vitals vitals;
    std::uint32_t respawnTick = 0;
    std::uint32_t deaths = 0;
    PlayerInfo info;
    bool isBot = false;
    std::optional<BotBrain> brain;
    std::uint32_t botSeq = 0;
  };
  struct PendingShot {
    std::uint8_t shooter;
    ShotInfo shot;
    float viewTick;
  };

  void handlePacket(const NetEvent &event);
  void handleHello(PeerId peer, std::span<const std::uint8_t> packet);
  void handleUserCmds(Client &client, std::span<const std::uint8_t> packet);
  void removePeer(PeerId peer);
  void simulate(std::uint8_t slot, Client &client);
  void resolveShot(const PendingShot &shot);
  void respawn(std::uint8_t slot, Client &client, const SpawnPoint &spawn);
  void startRound();
  void runRules();
  [[nodiscard]] Team smallerTeam() const;
  [[nodiscard]] SpawnPoint spawnFor(std::uint8_t slot, const Client &client, int indexInTeam) const;
  void sendEvent(std::uint8_t slot, const GameEvent &event);
  void broadcast(const GameEvent &event, int except = -1);
  void sendSnapshots();
  [[nodiscard]] Client *clientOfPeer(PeerId peer);

  Transport &m_transport;
  const CollisionWorld &m_world;
  const WeaponDb &m_weapons;
  ServerConfig m_config;
  RoundManager m_rules;
  FixedHitboxProvider m_hitboxes;
  LagCompensation m_lag;
  std::vector<PendingShot> m_pendingShots;
  VisibilityFn m_visibility;
  std::array<std::optional<Client>, kMaxPlayerSlots> m_clients;
  std::unordered_map<PeerId, std::uint8_t> m_slotOfPeer;
  std::uint32_t m_tick = 0;
  bool m_matchRestarted = false;
  ServerStats m_stats;
};
} // namespace Paper
