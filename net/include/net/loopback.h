#pragma once

#include "net/transport.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Paper {
// Link quality applied to every packet (each direction separately).
struct NetworkConditions {
  // One-way delay in milliseconds, plus uniform jitter in [-jitter, +jitter].
  double latencyMs = 0.0;
  double jitterMs = 0.0;
  // Unreliable packets only: probability of dropping / duplicating.
  double loss = 0.0;
  double duplicate = 0.0;
};

// An in-memory network with a virtual clock. Fully deterministic for a given
// seed: nothing here reads the real clock or global state. Reliable packets
// are never lost and arrive in order (as ENet guarantees), but still see
// latency; unreliable packets may be lost, duplicated and reordered (jitter).
class LoopbackNetwork {
public:
  class Endpoint;

  explicit LoopbackNetwork(std::uint64_t seed = 1);
  ~LoopbackNetwork();
  LoopbackNetwork(const LoopbackNetwork &) = delete;
  LoopbackNetwork &operator=(const LoopbackNetwork &) = delete;

  // Its `id()` is the peer id other endpoints use to address it.
  Endpoint &addEndpoint();
  [[nodiscard]] Endpoint &endpoint(PeerId id) { return *m_endpoints[id]; }

  void setConditions(const NetworkConditions &conditions) { m_conditions = conditions; }
  [[nodiscard]] const NetworkConditions &conditions() const { return m_conditions; }

  // Moves the virtual clock forward and delivers everything now due.
  void advance(std::int64_t nanoseconds);
  [[nodiscard]] std::int64_t now() const { return m_now; }

  [[nodiscard]] std::uint64_t packetsSent() const { return m_sent; }
  [[nodiscard]] std::uint64_t packetsDropped() const { return m_dropped; }

  class Endpoint final : public Transport {
  public:
    [[nodiscard]] PeerId id() const { return m_id; }
    // Starts a connection to `target`; both sides see Connected one
    // latency later (no real handshake is simulated).
    void connect(PeerId target);

    void send(PeerId peer, Channel channel, std::span<const std::uint8_t> data) override;
    [[nodiscard]] bool poll(NetEvent &event) override;
    void disconnect(PeerId peer) override;

  private:
    friend class LoopbackNetwork;
    Endpoint(LoopbackNetwork &network, PeerId id) : m_network(network), m_id(id) {}
    LoopbackNetwork &m_network;
    PeerId m_id;
    std::deque<NetEvent> m_inbox;
  };

private:
  struct Packet {
    std::int64_t deliverAt;
    std::uint64_t order;
    PeerId from;
    PeerId to;
    NetEvent event;
    bool operator>(const Packet &o) const {
      return deliverAt != o.deliverAt ? deliverAt > o.deliverAt : order > o.order;
    }
  };

  void enqueue(PeerId from, PeerId to, NetEvent event);
  std::uint64_t nextRandom();
  double random01();

  std::int64_t m_now = 0;
  std::uint64_t m_rng;
  std::uint64_t m_order = 0;
  std::uint64_t m_sent = 0;
  std::uint64_t m_dropped = 0;
  NetworkConditions m_conditions;
  std::vector<std::unique_ptr<Endpoint>> m_endpoints;
  std::priority_queue<Packet, std::vector<Packet>, std::greater<>> m_inFlight;
  // Latest reliable delivery time per (from, to) link, to keep order.
  std::unordered_map<std::uint64_t, std::int64_t> m_reliableTail;
};
} // namespace Paper
