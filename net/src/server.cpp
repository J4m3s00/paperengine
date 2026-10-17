#include "net/server.h"

#include "base/profiler.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace Paper {
namespace {
constexpr std::size_t kSnapshotHistory = 64;
} // namespace

std::vector<SpawnPoint> spawnPoints(const MapData &map) {
  std::vector<SpawnPoint> out;
  for (const MapEntity &e : map.entitiesOfType("spawn")) {
    out.push_back({e.origin, e.yaw});
  }
  return out;
}

Server::Server(Transport &transport, const CollisionWorld &world, const WeaponDb &weapons,
               ServerConfig config)
    : m_transport(transport), m_world(world), m_weapons(weapons), m_config(std::move(config)) {
  if (m_config.spawns.empty()) {
    m_config.spawns.push_back({glm::vec3(0.0F, 2.0F, 0.0F), 0.0F});
  }
}

int Server::playerCount() const {
  return static_cast<int>(std::count_if(m_clients.begin(), m_clients.end(),
                                        [](const auto &c) { return c.has_value(); }));
}

const PlayerState *Server::playerState(std::uint8_t slot) const {
  return slot < m_clients.size() && m_clients[slot] ? &m_clients[slot]->state : nullptr;
}

std::size_t Server::queuedCmds(std::uint8_t slot) const {
  return slot < m_clients.size() && m_clients[slot] ? m_clients[slot]->queue.size() : 0;
}

const Vitals *Server::vitals(std::uint8_t slot) const {
  return slot < m_clients.size() && m_clients[slot] ? &m_clients[slot]->vitals : nullptr;
}

const CombatState *Server::combat(std::uint8_t slot) const {
  return slot < m_clients.size() && m_clients[slot] ? &m_clients[slot]->combat : nullptr;
}

int Server::addBot(const SpawnPoint &spawn, bool moving) {
  const auto free = std::find_if(m_clients.begin(), m_clients.end(),
                                 [](const auto &c) { return !c.has_value(); });
  if (free == m_clients.end()) {
    return -1;
  }
  const auto slot = static_cast<std::uint8_t>(free - m_clients.begin());
  Client bot;
  bot.isBot = true;
  bot.name = "bot" + std::to_string(slot);
  bot.state.origin = spawn.origin;
  bot.yaw = spawn.yaw;
  bot.combat = makeLoadout(m_weapons);
  if (moving) {
    bot.brain.emplace(slot + 1);
  }
  *free = std::move(bot);
  return slot;
}

Server::Client *Server::clientOfPeer(PeerId peer) {
  const auto it = m_slotOfPeer.find(peer);
  return it == m_slotOfPeer.end() ? nullptr : &*m_clients[it->second];
}

void Server::removePeer(PeerId peer) {
  const auto it = m_slotOfPeer.find(peer);
  if (it == m_slotOfPeer.end()) {
    return;
  }
  m_lag.clear(it->second);
  spdlog::info("player '{}' left (slot {})", m_clients[it->second]->name, it->second);
  m_clients[it->second].reset();
  m_slotOfPeer.erase(it);
}

void Server::handleHello(PeerId peer, std::span<const std::uint8_t> packet) {
  if (m_slotOfPeer.contains(peer)) {
    return;
  }
  const auto hello = decodeHello(packet);
  if (!hello) {
    ++m_stats.malformedPackets;
    return;
  }
  const auto reject = [&](const char *reason) {
    m_transport.send(peer, Channel::Reliable, encode(RejectMsg{reason}));
    m_transport.disconnect(peer);
  };
  if (hello->protocolVersion != kProtocolVersion) {
    reject("protocol version mismatch");
    return;
  }
  const auto free = std::find_if(m_clients.begin(), m_clients.end(),
                                 [](const auto &c) { return !c.has_value(); });
  if (free == m_clients.end()) {
    reject("server full");
    return;
  }
  const auto slot = static_cast<std::uint8_t>(free - m_clients.begin());
  const SpawnPoint &spawn = m_config.spawns[slot % m_config.spawns.size()];
  Client client;
  client.peer = peer;
  client.name = hello->name;
  client.state.origin = spawn.origin;
  client.yaw = spawn.yaw;
  client.combat = makeLoadout(m_weapons);
  *free = std::move(client);
  m_slotOfPeer[peer] = slot;
  spdlog::info("player '{}' joined (slot {})", hello->name, slot);
  m_transport.send(peer, Channel::Reliable,
                   encode(AcceptMsg{slot, m_config.tickRate, m_config.snapshotRate, m_tick,
                                    m_config.map}));
}

void Server::handleUserCmds(Client &client, std::span<const std::uint8_t> packet) {
  const auto msg = decodeUserCmds(packet);
  if (!msg) {
    ++m_stats.malformedPackets;
    return;
  }
  // Only the newest acknowledgement counts (packets may arrive reordered).
  client.ackedSnapshot = std::max(client.ackedSnapshot, msg->ackedSnapshot);
  for (const NetUserCmd &cmd : msg->cmds) {
    // Redundant copies of cmds we already have are skipped.
    if (client.haveSeq && cmd.seq <= client.newestSeq) {
      continue;
    }
    client.haveSeq = true;
    client.newestSeq = cmd.seq;
    client.queue.push_back(cmd);
  }
  while (client.queue.size() > m_config.maxQueuedCmds) {
    client.queue.pop_front();
    ++m_stats.cmdsDropped;
  }
}

void Server::handlePacket(const NetEvent &event) {
  switch (event.type) {
  case NetEvent::Type::Connected:
    break; // wait for Hello
  case NetEvent::Type::Disconnected:
    removePeer(event.peer);
    break;
  case NetEvent::Type::Data: {
    const auto type = peekMessageType(event.data);
    if (!type) {
      ++m_stats.malformedPackets;
      break;
    }
    if (*type == MessageType::Hello) {
      handleHello(event.peer, event.data);
    } else if (*type == MessageType::UserCmds) {
      if (Client *client = clientOfPeer(event.peer)) {
        handleUserCmds(*client, event.data);
      }
    }
    break;
  }
  }
}

void Server::simulate(std::uint8_t slot, Client &client) {
  UserCmd cmd;
  std::uint32_t seq = 0;
  bool real = false;
  if (client.isBot) {
    cmd = client.brain ? client.brain->next() : UserCmd{};
    seq = ++client.botSeq;
    client.executedSeq = seq;
    real = true;
  } else if (!client.haveSeq) {
    return; // the player appears once its first input arrives
  } else if (!client.queue.empty()) {
    const NetUserCmd next = client.queue.front();
    client.queue.pop_front();
    cmd = next.cmd;
    seq = next.seq;
    client.lastCmd = cmd;
    client.executedSeq = seq;
    client.repeatedCmds = 0;
    ++m_stats.cmdsExecuted;
    real = true;
  } else {
    // Nothing arrived in time: keep going on the last input for a moment,
    // then stand still. Never run extra ticks to make up for it.
    ++m_stats.starvedTicks;
    cmd = client.repeatedCmds < m_config.maxRepeatedCmds ? client.lastCmd : UserCmd{};
    cmd.jump = false;
    ++client.repeatedCmds;
  }
  if (!client.vitals.alive) {
    return; // dead players do not move or shoot until they respawn
  }
  client.yaw = cmd.yaw;
  client.pitch = cmd.pitch;
  movePlayer(client.state, cmd, m_world, m_config.movement, weaponMaxSpeed(client.combat, m_weapons),
             1.0F / static_cast<float>(m_config.tickRate));
  if (real) {
    const WeaponEvents ev =
        stepWeapon(client.combat, cmd, client.state, eyePosition(client.state), seq, m_weapons);
    if (ev.fired) {
      m_pendingShots.push_back({slot, ev.shot, cmd.viewTick});
    }
  }
}

void Server::sendEvent(std::uint8_t slot, const GameEvent &event) {
  if (m_clients[slot] && !m_clients[slot]->isBot) {
    m_transport.send(m_clients[slot]->peer, Channel::Reliable, encode(event));
  }
}

void Server::broadcast(const GameEvent &event, int except) {
  for (std::uint8_t slot = 0; slot < m_clients.size(); ++slot) {
    if (static_cast<int>(slot) != except) {
      sendEvent(slot, event);
    }
  }
}

void Server::resolveShot(const PendingShot &pending) {
  PAPER_ZONE("Server::resolveShot");
  Client &shooter = *m_clients[pending.shooter];
  const WeaponDef &weapon = m_weapons.at(pending.shot.weapon);
  const glm::vec3 &origin = pending.shot.origin;
  const glm::vec3 &dir = pending.shot.direction;
  ++m_stats.shotsFired;

  // Where the shooter's screen was: other players are put back to that time.
  const std::uint32_t maxRewind = static_cast<std::uint32_t>(
      m_config.maxRewindSeconds * static_cast<float>(m_config.tickRate));
  const double viewTick =
      pending.viewTick > 0.0F
          ? LagCompensation::clampViewTick(pending.viewTick, m_tick, maxRewind)
          : static_cast<double>(m_tick);

  const TraceResult world = m_world.traceRay(origin, origin + dir * weapon.range);
  const float worldDistance = world.fraction * weapon.range;

  int victim = -1;
  HitboxHit best;
  best.distance = worldDistance;
  for (std::uint8_t slot = 0; slot < m_clients.size(); ++slot) {
    if (slot == pending.shooter || !m_clients[slot] || !m_clients[slot]->vitals.alive) {
      continue;
    }
    const Client &target = *m_clients[slot];
    const auto rewound = m_lag.poseAt(slot, viewTick);
    const PlayerPose pose = rewound ? *rewound : poseOf(target.state, target.yaw, target.pitch);
    HitboxSet set;
    m_hitboxes.hitboxes(pose, set);
    if (const auto hit = rayVsHitboxes(set, origin, dir, best.distance)) {
      best = *hit;
      victim = slot;
    }
  }

  GameEvent shotEvent;
  shotEvent.type = GameEvent::Type::Shot;
  shotEvent.actor = pending.shooter;
  shotEvent.weapon = pending.shot.weapon;
  shotEvent.from = origin;
  shotEvent.to = origin + dir * best.distance;
  broadcast(shotEvent, pending.shooter);

  if (victim < 0) {
    return;
  }
  Client &target = *m_clients[static_cast<std::size_t>(victim)];
  const bool head = best.group == HitGroup::Head;
  const float damage =
      hitDamage(weapon, head, hitGroupMultiplier(best.group), best.distance);
  const bool leg = best.group == HitGroup::LeftLeg || best.group == HitGroup::RightLeg;
  const ArmorResult armored = target.vitals.armor > 0 && !leg
                                  ? applyArmor(weapon, damage, static_cast<float>(target.vitals.armor))
                                  : ArmorResult{damage, 0.0F};
  const int loss = std::max(1, static_cast<int>(std::lround(armored.health)));
  target.vitals.armor = static_cast<std::uint8_t>(
      std::max(0, target.vitals.armor - static_cast<int>(std::lround(armored.armorLost))));
  target.vitals.health = static_cast<std::uint8_t>(std::max(0, target.vitals.health - loss));
  ++m_stats.hits;

  GameEvent hit;
  hit.type = GameEvent::Type::Hit;
  hit.actor = pending.shooter;
  hit.target = static_cast<std::uint8_t>(victim);
  hit.weapon = pending.shot.weapon;
  hit.damage = static_cast<std::uint8_t>(std::min(loss, 255));
  hit.headshot = head;
  hit.to = best.point;
  sendEvent(pending.shooter, hit);
  sendEvent(static_cast<std::uint8_t>(victim), hit);

  if (target.vitals.health == 0) {
    target.vitals.alive = false;
    target.respawnTick =
        m_tick + static_cast<std::uint32_t>(m_config.respawnSeconds * static_cast<float>(m_config.tickRate));
    ++target.deaths;
    ++m_stats.kills;
    m_lag.clear(static_cast<std::uint8_t>(victim));
    GameEvent kill = hit;
    kill.type = GameEvent::Type::Kill;
    broadcast(kill);
  }
  (void)shooter;
}

void Server::respawn(std::uint8_t slot, Client &client) {
  const SpawnPoint &spawn = m_config.spawns[(slot + client.deaths) % m_config.spawns.size()];
  client.state = PlayerState{};
  client.state.origin = spawn.origin;
  client.yaw = spawn.yaw;
  client.pitch = 0.0F;
  client.combat = makeLoadout(m_weapons);
  client.vitals = Vitals{};
  m_lag.clear(slot);
}

void Server::sendSnapshots() {
  PAPER_ZONE("Server::sendSnapshots");
  for (std::uint8_t viewer = 0; viewer < m_clients.size(); ++viewer) {
    if (!m_clients[viewer] || m_clients[viewer]->isBot) {
      continue;
    }
    Client &to = *m_clients[viewer];
    Snapshot snapshot;
    snapshot.tick = m_tick;
    snapshot.ackedCmd = to.executedSeq;
    snapshot.cmdQueue = static_cast<std::uint8_t>(std::min<std::size_t>(to.queue.size(), 31));
    snapshot.hasSelf = true;
    snapshot.self = to.combat;
    for (std::uint8_t other = 0; other < m_clients.size(); ++other) {
      if (!m_clients[other] || (other != viewer && m_visibility && !m_visibility(viewer, other))) {
        continue;
      }
      const Client &c = *m_clients[other];
      snapshot.players.push_back({other, c.state, c.yaw, c.pitch, c.vitals, c.combat.weapon});
    }
    const Snapshot *baseline = nullptr;
    if (to.ackedSnapshot != 0) {
      const auto it = std::find_if(to.history.begin(), to.history.end(),
                                   [&](const Snapshot &s) { return s.tick == to.ackedSnapshot; });
      baseline = it == to.history.end() ? nullptr : &*it;
    }
    const auto packet = encode(snapshot, baseline);
    m_transport.send(to.peer, Channel::Unreliable, packet);
    ++m_stats.snapshotsSent;
    m_stats.snapshotBytesSent += packet.size();
    m_stats.fullSnapshotsSent += baseline ? 0 : 1;
    to.history.push_back(std::move(snapshot));
    while (to.history.size() > kSnapshotHistory) {
      to.history.pop_front();
    }
  }
}

void Server::tick() {
  PAPER_ZONE("Server::tick");
  NetEvent event;
  while (m_transport.poll(event)) {
    handlePacket(event);
  }
  ++m_tick;
  ++m_stats.ticks;
  for (std::uint8_t slot = 0; slot < m_clients.size(); ++slot) {
    if (m_clients[slot]) {
      simulate(slot, *m_clients[slot]);
    }
  }
  // Everyone has moved: record where they are, then resolve this tick's shots
  // against those poses so the result does not depend on slot order.
  for (std::uint8_t slot = 0; slot < m_clients.size(); ++slot) {
    if (m_clients[slot] && m_clients[slot]->vitals.alive) {
      const Client &c = *m_clients[slot];
      m_lag.record(m_tick, slot, poseOf(c.state, c.yaw, c.pitch));
    }
  }
  for (const PendingShot &shot : m_pendingShots) {
    if (m_clients[shot.shooter]) {
      resolveShot(shot);
    }
  }
  m_pendingShots.clear();
  for (std::uint8_t slot = 0; slot < m_clients.size(); ++slot) {
    Client *c = m_clients[slot] ? &*m_clients[slot] : nullptr;
    if (c && !c->vitals.alive && m_tick >= c->respawnTick) {
      respawn(slot, *c);
    }
  }
  const int interval = std::max(1, m_config.tickRate / std::max<int>(1, m_config.snapshotRate));
  if (m_tick % static_cast<std::uint32_t>(interval) == 0) {
    sendSnapshots();
  }
}
} // namespace Paper
