#include "net/conditioned_transport.h"

#include <algorithm>
#include <cmath>

namespace Paper {
double ConditionedTransport::random01() {
  m_rng += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = m_rng;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
}

std::vector<std::int64_t> ConditionedTransport::schedule(Channel channel, std::int64_t &tail) {
  const bool reliable = channel == Channel::Reliable;
  if (!reliable && random01() < m_conditions.loss) {
    return {};
  }
  const int copies = !reliable && random01() < m_conditions.duplicate ? 2 : 1;
  std::vector<std::int64_t> times;
  for (int i = 0; i < copies; ++i) {
    const double jitter = (random01() * 2.0 - 1.0) * m_conditions.jitterMs;
    std::int64_t at = m_now + static_cast<std::int64_t>(
                                  std::llround(std::max(0.0, m_conditions.latencyMs + jitter) * 1e6));
    if (reliable) {
      at = std::max(at, tail);
      tail = at;
    }
    times.push_back(at);
  }
  return times;
}

void ConditionedTransport::send(PeerId peer, Channel channel, std::span<const std::uint8_t> data) {
  for (const std::int64_t at : schedule(channel, m_outTail)) {
    m_outgoing.push({at, m_order++, peer, channel, {data.begin(), data.end()}, {}, true});
  }
}

void ConditionedTransport::update(std::int64_t nowNs) {
  m_now = nowNs;
  // Pull what the inner transport received and delay it.
  NetEvent e;
  while (m_inner.poll(e)) {
    // Connection events pass through without delay: they are not game traffic.
    if (e.type != NetEvent::Type::Data) {
      m_ready.push(std::move(e));
      continue;
    }
    for (const std::int64_t at : schedule(e.channel, m_inTail)) {
      m_incoming.push({at, m_order++, e.peer, e.channel, {}, e, false});
    }
  }
  while (!m_outgoing.empty() && m_outgoing.top().at <= m_now) {
    const Delayed &d = m_outgoing.top();
    m_inner.send(d.peer, d.channel, d.data);
    m_outgoing.pop();
  }
  while (!m_incoming.empty() && m_incoming.top().at <= m_now) {
    m_ready.push(m_incoming.top().event);
    m_incoming.pop();
  }
}

bool ConditionedTransport::poll(NetEvent &event) {
  if (m_ready.empty()) {
    return false;
  }
  event = std::move(m_ready.front());
  m_ready.pop();
  return true;
}
} // namespace Paper
