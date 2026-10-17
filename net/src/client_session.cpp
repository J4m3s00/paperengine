#include "net/client_session.h"

#include "base/profiler.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace Paper {
namespace {
constexpr std::size_t kSnapshotHistory = 64;
constexpr std::size_t kMaxPending = 512;
// A server state this close to the prediction is "the same".
constexpr float kOriginTolerance = 0.01F;
constexpr float kVelocityTolerance = 0.1F;

float lerpAngle(float a, float b, float t) {
  constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
  float d = std::fmod(b - a, twoPi);
  if (d > std::numbers::pi_v<float>)
    d -= twoPi;
  if (d < -std::numbers::pi_v<float>)
    d += twoPi;
  return a + d * t;
}

bool matches(const PlayerState &a, const PlayerState &b) {
  return glm::distance(a.origin, b.origin) <= kOriginTolerance &&
         glm::distance(a.velocity, b.velocity) <= kVelocityTolerance &&
         a.onGround == b.onGround && a.ducked == b.ducked;
}
} // namespace

ClientSession::ClientSession(Transport &transport, PeerId server,
                             const CollisionWorld &world,
                             const WeaponDb &weapons, MovementParams params,
                             std::string name, ClientConfig config)
    : m_transport(transport), m_server(server), m_world(world),
      m_weapons(weapons), m_params(params), m_name(std::move(name)),
      m_config(config), m_combat(makeLoadout(weapons)) {}

const std::vector<NetPlayer> &ClientSession::latestPlayers() const {
  static const std::vector<NetPlayer> none;
  return m_snapshots.empty() ? none : m_snapshots.back().players;
}

double ClientSession::phaseSecondsLeft() const {
  if (m_match.phaseEndTick == 0 || !m_haveClock) {
    return 0.0;
  }
  const double serverNow = static_cast<double>(m_localTick) * dt() + m_clockOffset;
  return std::max(0.0, static_cast<double>(m_match.phaseEndTick) * dt() - serverNow);
}

std::vector<ShotInfo> ClientSession::takeLocalShots() {
  return std::exchange(m_localShots, {});
}
std::vector<GameEvent> ClientSession::takeEvents() {
  return std::exchange(m_events, {});
}

void ClientSession::tick(const UserCmd &input) {
  PAPER_ZONE("ClientSession::tick");
  ++m_localTick;
  receive();
  if (m_status == Status::Playing) {
    UserCmd stamped = input;
    // The server tick our screen shows other players at (lag compensation).
    stamped.viewTick =
        m_haveClock
            ? static_cast<float>(std::max(
                  0.0, (static_cast<double>(m_localTick) * dt() +
                        m_clockOffset - m_config.interpolationDelayMs * 0.001) /
                           dt()))
            : 0.0F;
    const UserCmd cmd = quantizeUserCmd(stamped);
    const std::uint32_t seq = m_nextSeq++;
    if (m_vitals.alive) {
      movePlayer(m_predicted, restrictCmd(cmd, m_match.phase), m_world, m_params,
                 weaponMaxSpeed(m_combat, m_weapons), static_cast<float>(dt()));
      if (phaseAllowsShooting(m_match.phase)) {
        const WeaponEvents ev = stepWeapon(
            m_combat, cmd, m_predicted, eyePosition(m_predicted), seq, m_weapons);
        if (ev.fired) {
          m_localShots.push_back(ev.shot);
        }
      }
    }
    m_pending.push_back({seq, cmd, m_localTick, m_predicted, m_combat});
    while (m_pending.size() > kMaxPending) {
      m_pending.pop_front();
    }
    sendCmds();
    m_errorOffset *=
        std::exp(-static_cast<float>(dt()) / m_config.correctionDecay);
  }
  m_stats.snapshotAgeMs =
      static_cast<double>(m_localTick - m_lastSnapshotLocalTick) * dt() *
      1000.0;
}

void ClientSession::receive() {
  NetEvent event;
  while (m_transport.poll(event)) {
    m_stats.bytesReceived += event.data.size();
    switch (event.type) {
    case NetEvent::Type::Connected: {
      const auto hello = encode(HelloMsg{kProtocolVersion, m_name});
      m_transport.send(m_server, Channel::Reliable, hello);
      m_stats.bytesSent += hello.size();
      m_status = Status::Joining;
      break;
    }
    case NetEvent::Type::Disconnected:
      if (m_status != Status::Rejected) {
        m_status = Status::Disconnected;
      }
      break;
    case NetEvent::Type::Data: {
      const auto type = peekMessageType(event.data);
      if (!type) {
        break;
      }
      if (*type == MessageType::Accept) {
        onAccept(event.data);
      } else if (*type == MessageType::Reject) {
        if (const auto reject = decodeReject(event.data)) {
          m_rejectReason = reject->reason;
          m_status = Status::Rejected;
        }
      } else if (*type == MessageType::Event) {
        onEvent(event.data);
      } else if (*type == MessageType::Snapshot) {
        onSnapshot(event.data, event.data.size());
      }
      break;
    }
    }
  }
}

void ClientSession::onEvent(std::span<const std::uint8_t> packet) {
  if (const auto event = decodeEvent(packet)) {
    m_events.push_back(*event);
  }
}

void ClientSession::adoptAccept(const AcceptMsg &accept) {
  m_slot = accept.slot;
  m_status = Status::Joining;
}

void ClientSession::onAccept(std::span<const std::uint8_t> packet) {
  if (const auto accept = decodeAccept(packet)) {
    m_slot = accept->slot;
    if (accept->tickRate != m_config.tickRate) {
      spdlog::warn("server tick rate {} differs from client {}",
                   accept->tickRate, m_config.tickRate);
    }
  }
}

void ClientSession::onSnapshot(std::span<const std::uint8_t> packet,
                               std::size_t) {
  if (m_status != Status::Joining && m_status != Status::Playing) {
    return;
  }
  const auto baselineTick = snapshotBaselineTick(packet);
  if (!baselineTick) {
    return;
  }
  const Snapshot *baseline = nullptr;
  if (*baselineTick != 0) {
    const auto it = std::find_if(
        m_snapshots.begin(), m_snapshots.end(),
        [&](const Snapshot &s) { return s.tick == *baselineTick; });
    if (it == m_snapshots.end()) {
      return;
    }
    baseline = &*it;
  }
  auto decoded = decodeSnapshot(packet, baseline);
  if (!decoded || decoded->tick <= m_newestSnapshotTick) {
    return; // malformed, duplicate or older than what we have
  }
  Snapshot &snapshot = *decoded;
  ++m_stats.snapshotsReceived;
  m_newestSnapshotTick = snapshot.tick;
  m_match = snapshot.match;
  m_lastSnapshotLocalTick = m_localTick;

  const double sample = static_cast<double>(snapshot.tick) * dt() -
                        static_cast<double>(m_localTick) * dt();
  m_clockOffset =
      m_haveClock ? m_clockOffset + (sample - m_clockOffset) * 0.1 : sample;
  m_haveClock = true;

  const int error =
      m_config.targetCmdQueue - static_cast<int>(snapshot.cmdQueue);
  m_timeScale = 1.0 + std::clamp(error * 0.01, -0.03, 0.05);

  const auto self =
      std::find_if(snapshot.players.begin(), snapshot.players.end(),
                   [&](const NetPlayer &p) { return p.slot == m_slot; });
  if (self != snapshot.players.end()) {
    m_vitals = self->vitals;
    if (m_status == Status::Joining) {
      m_predicted = self->state;
      if (snapshot.hasSelf) {
        m_combat = snapshot.self;
      }
      m_status = Status::Playing;
    } else {
      reconcile(snapshot, *self);
    }
  }
  m_snapshots.push_back(std::move(snapshot));
  while (m_snapshots.size() > kSnapshotHistory) {
    m_snapshots.pop_front();
  }
}

void ClientSession::reconcile(const Snapshot &snapshot, const NetPlayer &self) {
  PAPER_ZONE("ClientSession::reconcile");
  const std::uint32_t acked = snapshot.ackedCmd;
  if (acked == 0) {
    return;
  }
  const auto it =
      std::find_if(m_pending.begin(), m_pending.end(),
                   [&](const Pending &p) { return p.seq == acked; });
  const bool found = it != m_pending.end();
  if (found) {
    const double rtt =
        static_cast<double>(m_localTick - it->sentTick) * dt() * 1000.0;
    m_stats.inputRttMs =
        m_stats.inputRttMs == 0.0 ? rtt : m_stats.inputRttMs * 0.9 + rtt * 0.1;
  }
  const bool agrees = found && matches(it->after, self.state) &&
                      (!snapshot.hasSelf ||
                       combatEquivalent(it->combatAfter, snapshot.self, acked));
  while (!m_pending.empty() && m_pending.front().seq <= acked) {
    m_pending.pop_front();
  }
  if (!found || agrees) {
    return;
  }

  ++m_stats.corrections;
  const glm::vec3 before = m_predicted.origin;
  PlayerState state = self.state;
  CombatState combat = snapshot.hasSelf ? snapshot.self : m_combat;
  for (Pending &p : m_pending) {
    if (self.vitals.alive) {
      movePlayer(state, restrictCmd(p.cmd, snapshot.match.phase), m_world, m_params,
                 weaponMaxSpeed(combat, m_weapons), static_cast<float>(dt()));
      if (phaseAllowsShooting(snapshot.match.phase)) {
        stepWeapon(combat, p.cmd, state, eyePosition(state), p.seq, m_weapons);
      }
    }
    p.after = state;
    p.combatAfter = combat;
  }
  m_predicted = state;
  m_combat = combat;
  const glm::vec3 jump = before - m_predicted.origin;
  m_stats.lastCorrection = glm::length(jump);
  m_errorOffset = glm::length(jump) > m_config.snapDistance
                      ? glm::vec3(0.0F)
                      : m_errorOffset + jump;
}

void ClientSession::sendCmds() {
  UserCmdsMsg msg;
  msg.ackedSnapshot = m_newestSnapshotTick;
  const std::size_t count =
      std::min<std::size_t>(m_pending.size(), kMaxCmdsPerPacket);
  for (std::size_t i = m_pending.size() - count; i < m_pending.size(); ++i) {
    msg.cmds.push_back({m_pending[i].seq, m_pending[i].cmd});
  }
  const auto packet = encode(msg);
  m_transport.send(m_server, Channel::Unreliable, packet);
  m_stats.bytesSent += packet.size();
  ++m_stats.cmdsSent;
}

std::vector<NetPlayer> ClientSession::remotePlayers() const {
  std::vector<NetPlayer> out;
  if (m_snapshots.empty()) {
    return out;
  }
  const double renderTime = static_cast<double>(m_localTick) * dt() +
                            m_clockOffset -
                            m_config.interpolationDelayMs * 0.001;
  const auto timeOf = [&](const Snapshot &s) {
    return static_cast<double>(s.tick) * dt();
  };

  const Snapshot *newer = &m_snapshots.back();
  const Snapshot *older = newer;
  for (auto it = m_snapshots.rbegin(); it != m_snapshots.rend(); ++it) {
    if (timeOf(*it) <= renderTime) {
      older = &*it;
      break;
    }
    newer = &*it;
    older = &*it; // nothing older yet: hold the oldest
  }
  double t = 0.0;
  if (older != newer && timeOf(*newer) > timeOf(*older)) {
    t = std::clamp((renderTime - timeOf(*older)) /
                       (timeOf(*newer) - timeOf(*older)),
                   0.0, 1.0);
  }
  for (const NetPlayer &to : newer->players) {
    if (to.slot == m_slot) {
      continue;
    }
    NetPlayer p = to;
    const auto from =
        std::find_if(older->players.begin(), older->players.end(),
                     [&](const NetPlayer &o) { return o.slot == to.slot; });
    if (from != older->players.end()) {
      const auto ft = static_cast<float>(t);
      p.state.origin = glm::mix(from->state.origin, to.state.origin, ft);
      p.state.velocity = glm::mix(from->state.velocity, to.state.velocity, ft);
      p.yaw = lerpAngle(from->yaw, to.yaw, ft);
      p.pitch = lerpAngle(from->pitch, to.pitch, ft);
    }
    out.push_back(p);
  }
  return out;
}
} // namespace Paper
