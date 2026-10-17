#include "net/loopback.h"

#include <algorithm>
#include <cmath>

namespace Paper {
LoopbackNetwork::LoopbackNetwork(std::uint64_t seed) : m_rng(seed) {}
LoopbackNetwork::~LoopbackNetwork() = default;

LoopbackNetwork::Endpoint &LoopbackNetwork::addEndpoint() {
  m_endpoints.push_back(std::unique_ptr<Endpoint>(
      new Endpoint(*this, static_cast<PeerId>(m_endpoints.size()))));
  return *m_endpoints.back();
}

// splitmix64: tiny, fast and identical on every platform.
std::uint64_t LoopbackNetwork::nextRandom() {
  m_rng += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = m_rng;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

double LoopbackNetwork::random01() {
  return static_cast<double>(nextRandom() >> 11) * (1.0 / 9007199254740992.0);
}

void LoopbackNetwork::enqueue(PeerId from, PeerId to, NetEvent event) {
  ++m_sent;
  const bool reliable = event.channel == Channel::Reliable;
  if (!reliable && random01() < m_conditions.loss) {
    ++m_dropped;
    return;
  }
  const int copies = !reliable && random01() < m_conditions.duplicate ? 2 : 1;
  for (int i = 0; i < copies; ++i) {
    const double jitter = (random01() * 2.0 - 1.0) * m_conditions.jitterMs;
    const double delayMs = std::max(0.0, m_conditions.latencyMs + jitter);
    std::int64_t at = m_now + static_cast<std::int64_t>(std::llround(delayMs * 1e6));
    if (reliable) {
      const std::uint64_t link = (static_cast<std::uint64_t>(from) << 32) | to;
      std::int64_t &tail = m_reliableTail[link];
      at = std::max(at, tail);
      tail = at;
    }
    m_inFlight.push({at, m_order++, from, to, event});
  }
}

void LoopbackNetwork::advance(std::int64_t nanoseconds) {
  m_now += nanoseconds;
  while (!m_inFlight.empty() && m_inFlight.top().deliverAt <= m_now) {
    Packet p = m_inFlight.top();
    m_inFlight.pop();
    Endpoint &to = *m_endpoints[p.to];
    to.m_inbox.push_back(std::move(p.event));
  }
}

void LoopbackNetwork::Endpoint::connect(PeerId target) {
  NetEvent hello;
  hello.type = NetEvent::Type::Connected;
  hello.peer = m_id; // the receiver sees who is calling
  hello.channel = Channel::Reliable;
  m_network.enqueue(m_id, target, hello);
  NetEvent reply;
  reply.type = NetEvent::Type::Connected;
  reply.peer = target;
  reply.channel = Channel::Reliable;
  m_network.enqueue(target, m_id, reply);
}

void LoopbackNetwork::Endpoint::send(PeerId peer, Channel channel,
                                     std::span<const std::uint8_t> data) {
  NetEvent e;
  e.type = NetEvent::Type::Data;
  e.peer = m_id;
  e.channel = channel;
  e.data.assign(data.begin(), data.end());
  m_network.enqueue(m_id, peer, std::move(e));
}

bool LoopbackNetwork::Endpoint::poll(NetEvent &event) {
  if (m_inbox.empty()) {
    return false;
  }
  event = std::move(m_inbox.front());
  m_inbox.pop_front();
  return true;
}

void LoopbackNetwork::Endpoint::disconnect(PeerId peer) {
  NetEvent bye;
  bye.type = NetEvent::Type::Disconnected;
  bye.peer = m_id;
  bye.channel = Channel::Reliable;
  m_network.enqueue(m_id, peer, bye);
  NetEvent self;
  self.type = NetEvent::Type::Disconnected;
  self.peer = peer;
  self.channel = Channel::Reliable;
  m_inbox.push_back(std::move(self));
}
} // namespace Paper
