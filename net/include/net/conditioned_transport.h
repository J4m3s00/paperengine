#pragma once

#include "net/loopback.h"
#include "net/transport.h"

#include <queue>

namespace Paper {
// Wraps any Transport and degrades it like `LoopbackNetwork` does (latency,
// jitter, loss, duplication, applied in both directions), so the real client
// can be played under bad network conditions. The owner supplies the clock:
// call `update(nowNs)` once per tick before using it.
class ConditionedTransport final : public Transport {
public:
  ConditionedTransport(Transport &inner, NetworkConditions conditions, std::uint64_t seed = 1)
      : m_inner(inner), m_conditions(conditions), m_rng(seed) {}

  void setConditions(const NetworkConditions &c) { m_conditions = c; }
  void update(std::int64_t nowNs);

  void send(PeerId peer, Channel channel, std::span<const std::uint8_t> data) override;
  [[nodiscard]] bool poll(NetEvent &event) override;
  void disconnect(PeerId peer) override { m_inner.disconnect(peer); }

private:
  struct Delayed {
    std::int64_t at;
    std::uint64_t order;
    PeerId peer;
    Channel channel;
    std::vector<std::uint8_t> data;
    NetEvent event;
    bool outgoing;
    bool operator>(const Delayed &o) const {
      return at != o.at ? at > o.at : order > o.order;
    }
  };
  using Queue = std::priority_queue<Delayed, std::vector<Delayed>, std::greater<>>;

  double random01();
  // Returns the delivery times for one packet (empty = lost).
  std::vector<std::int64_t> schedule(Channel channel, std::int64_t& tail);

  Transport &m_inner;
  NetworkConditions m_conditions;
  std::uint64_t m_rng;
  std::uint64_t m_order = 0;
  std::int64_t m_now = 0;
  std::int64_t m_outTail = 0;
  std::int64_t m_inTail = 0;
  Queue m_outgoing;
  Queue m_incoming;
  std::queue<NetEvent> m_ready;
};
} // namespace Paper
