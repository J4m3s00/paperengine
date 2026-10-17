#pragma once

#include "base/error.h"
#include "net/transport.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

namespace Paper {
// Transport over ENet/UDP. Channel 0 is reliable, channel 1 unreliable
// (sequenced: stale packets are dropped). Peer ids are ENet's per-host peer
// indices; a client talks to its server as peer id 0.
class EnetTransport final : public Transport {
public:
  // port 0 picks a free port (see `port()`).
  [[nodiscard]] static std::expected<std::unique_ptr<EnetTransport>, Error>
  createServer(std::uint16_t port, std::uint32_t maxPeers);
  [[nodiscard]] static std::expected<std::unique_ptr<EnetTransport>, Error> createClient();
  ~EnetTransport() override;
  EnetTransport(const EnetTransport &) = delete;
  EnetTransport &operator=(const EnetTransport &) = delete;

  // Starts connecting; Connected arrives through poll().
  [[nodiscard]] std::expected<PeerId, Error> connect(const std::string &host, std::uint16_t port);
  [[nodiscard]] std::uint16_t port() const;

  void send(PeerId peer, Channel channel, std::span<const std::uint8_t> data) override;
  [[nodiscard]] bool poll(NetEvent &event) override;
  void disconnect(PeerId peer) override;

  // Round-trip time ENet measured for the peer, in milliseconds.
  [[nodiscard]] std::uint32_t roundTripMs(PeerId peer) const;

private:
  struct Impl;
  explicit EnetTransport(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};
} // namespace Paper
