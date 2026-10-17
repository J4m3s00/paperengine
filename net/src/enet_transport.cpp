#include "net/enet_transport.h"

#include <enet/enet.h>

#include <mutex>

namespace Paper {
namespace {
constexpr enet_uint8 kReliableChannel = 0;
constexpr enet_uint8 kUnreliableChannel = 1;
constexpr size_t kChannelCount = 2;

// enet_initialize / enet_deinitialize are global; count the users.
std::mutex g_initMutex;
int g_initCount = 0;

bool acquireEnet() {
  std::lock_guard lock(g_initMutex);
  if (g_initCount == 0 && enet_initialize() != 0) {
    return false;
  }
  ++g_initCount;
  return true;
}

void releaseEnet() {
  std::lock_guard lock(g_initMutex);
  if (--g_initCount == 0) {
    enet_deinitialize();
  }
}
} // namespace

struct EnetTransport::Impl {
  ENetHost *host = nullptr;
  ~Impl() {
    if (host) {
      enet_host_destroy(host);
    }
    releaseEnet();
  }
  [[nodiscard]] ENetPeer *peer(PeerId id) const {
    return id < host->peerCount ? &host->peers[id] : nullptr;
  }
};

EnetTransport::EnetTransport(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
EnetTransport::~EnetTransport() = default;

std::expected<std::unique_ptr<EnetTransport>, Error>
EnetTransport::createServer(std::uint16_t port, std::uint32_t maxPeers) {
  if (!acquireEnet()) {
    return std::unexpected(Error{ErrorCode::Network, "enet_initialize failed"});
  }
  auto impl = std::make_unique<Impl>();
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  impl->host = enet_host_create(&address, maxPeers, kChannelCount, 0, 0);
  if (!impl->host) {
    return std::unexpected(Error{ErrorCode::Io, "cannot bind UDP port " + std::to_string(port)});
  }
  return std::unique_ptr<EnetTransport>(new EnetTransport(std::move(impl)));
}

std::expected<std::unique_ptr<EnetTransport>, Error> EnetTransport::createClient() {
  if (!acquireEnet()) {
    return std::unexpected(Error{ErrorCode::Network, "enet_initialize failed"});
  }
  auto impl = std::make_unique<Impl>();
  impl->host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
  if (!impl->host) {
    return std::unexpected(Error{ErrorCode::Io, "cannot create ENet client host"});
  }
  return std::unique_ptr<EnetTransport>(new EnetTransport(std::move(impl)));
}

std::expected<PeerId, Error> EnetTransport::connect(const std::string &host, std::uint16_t port) {
  ENetAddress address{};
  if (enet_address_set_host(&address, host.c_str()) != 0) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, "cannot resolve '" + host + "'"});
  }
  address.port = port;
  ENetPeer *peer = enet_host_connect(m_impl->host, &address, kChannelCount, 0);
  if (!peer) {
    return std::unexpected(Error{ErrorCode::Network, "no free ENet peer slot"});
  }
  return static_cast<PeerId>(peer->incomingPeerID);
}

std::uint16_t EnetTransport::port() const { return m_impl->host->address.port; }

void EnetTransport::send(PeerId id, Channel channel, std::span<const std::uint8_t> data) {
  ENetPeer *peer = m_impl->peer(id);
  if (!peer || peer->state != ENET_PEER_STATE_CONNECTED) {
    return;
  }
  const bool reliable = channel == Channel::Reliable;
  ENetPacket *packet = enet_packet_create(
      data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  if (packet && enet_peer_send(peer, reliable ? kReliableChannel : kUnreliableChannel, packet) < 0) {
    enet_packet_destroy(packet);
  }
}

bool EnetTransport::poll(NetEvent &out) {
  ENetEvent event;
  // Zero timeout: service the socket without blocking.
  if (enet_host_service(m_impl->host, &event, 0) <= 0) {
    return false;
  }
  out = NetEvent{};
  out.peer = static_cast<PeerId>(event.peer->incomingPeerID);
  switch (event.type) {
  case ENET_EVENT_TYPE_CONNECT:
    out.type = NetEvent::Type::Connected;
    break;
  case ENET_EVENT_TYPE_DISCONNECT:
    out.type = NetEvent::Type::Disconnected;
    break;
  case ENET_EVENT_TYPE_RECEIVE:
    out.type = NetEvent::Type::Data;
    out.channel = event.channelID == kReliableChannel ? Channel::Reliable : Channel::Unreliable;
    out.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
    enet_packet_destroy(event.packet);
    break;
  default:
    return poll(out);
  }
  return true;
}

void EnetTransport::disconnect(PeerId id) {
  if (ENetPeer *peer = m_impl->peer(id)) {
    enet_peer_disconnect(peer, 0);
  }
}

std::uint32_t EnetTransport::roundTripMs(PeerId id) const {
  const ENetPeer *peer = m_impl->peer(id);
  return peer ? peer->roundTripTime : 0;
}
} // namespace Paper
