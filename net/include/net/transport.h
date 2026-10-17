#pragma once

#include <cstdint>
#include <span>
#include <vector>

// What the netcode needs from a transport (ADR 0009). Implementations: ENet
// for real sockets and `LoopbackNetwork` (in-memory, with a network
// conditioner) for deterministic tests.
namespace Paper {
using PeerId = std::uint32_t;

enum class Channel : std::uint8_t {
  // Ordered and delivered exactly once (connection control, rare events).
  Reliable,
  // May be lost, duplicated or reordered (usercmds, snapshots).
  Unreliable,
};

struct NetEvent {
  enum class Type : std::uint8_t { Connected, Disconnected, Data };
  Type type = Type::Data;
  PeerId peer = 0;
  Channel channel = Channel::Reliable;
  std::vector<std::uint8_t> data;
};

class Transport {
public:
  virtual ~Transport() = default;
  virtual void send(PeerId peer, Channel channel, std::span<const std::uint8_t> data) = 0;
  // Next pending event, or false if there is none. Never blocks.
  [[nodiscard]] virtual bool poll(NetEvent &event) = 0;
  virtual void disconnect(PeerId peer) = 0;
};
} // namespace Paper
