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

Server::Server(Transport &transport, const CollisionWorld &world, ServerConfig config)
    : m_transport(transport), m_world(world), m_config(std::move(config)) {
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

Server::Client *Server::clientOfPeer(PeerId peer) {
  const auto it = m_slotOfPeer.find(peer);
  return it == m_slotOfPeer.end() ? nullptr : &*m_clients[it->second];
}

void Server::removePeer(PeerId peer) {
  const auto it = m_slotOfPeer.find(peer);
  if (it == m_slotOfPeer.end()) {
    return;
  }
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

void Server::simulate(Client &client) {
  if (!client.haveSeq) {
    return; // the player appears once its first input arrives
  }
  UserCmd cmd;
  if (!client.queue.empty()) {
    const NetUserCmd next = client.queue.front();
    client.queue.pop_front();
    cmd = next.cmd;
    client.lastCmd = cmd;
    client.executedSeq = next.seq;
    client.repeatedCmds = 0;
    ++m_stats.cmdsExecuted;
  } else {
    // Nothing arrived in time: keep going on the last input for a moment,
    // then stand still. Never run extra ticks to make up for it.
    ++m_stats.starvedTicks;
    cmd = client.repeatedCmds < m_config.maxRepeatedCmds ? client.lastCmd : UserCmd{};
    cmd.jump = false;
    ++client.repeatedCmds;
  }
  client.yaw = cmd.yaw;
  client.pitch = cmd.pitch;
  movePlayer(client.state, cmd, m_world, m_config.movement, kDefaultMaxSpeed,
             1.0F / static_cast<float>(m_config.tickRate));
}

void Server::sendSnapshots() {
  PAPER_ZONE("Server::sendSnapshots");
  for (std::uint8_t viewer = 0; viewer < m_clients.size(); ++viewer) {
    if (!m_clients[viewer]) {
      continue;
    }
    Client &to = *m_clients[viewer];
    Snapshot snapshot;
    snapshot.tick = m_tick;
    snapshot.ackedCmd = to.executedSeq;
    snapshot.cmdQueue = static_cast<std::uint8_t>(std::min<std::size_t>(to.queue.size(), 31));
    for (std::uint8_t other = 0; other < m_clients.size(); ++other) {
      if (!m_clients[other] || (other != viewer && m_visibility && !m_visibility(viewer, other))) {
        continue;
      }
      const Client &c = *m_clients[other];
      snapshot.players.push_back({other, c.state, c.yaw, c.pitch});
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
  for (auto &client : m_clients) {
    if (client) {
      simulate(*client);
    }
  }
  const int interval = std::max(1, m_config.tickRate / std::max<int>(1, m_config.snapshotRate));
  if (m_tick % static_cast<std::uint32_t>(interval) == 0) {
    sendSnapshots();
  }
}
} // namespace Paper
