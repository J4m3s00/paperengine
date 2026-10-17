#include <doctest/doctest.h>

#include "net/enet_transport.h"

#include <array>
#include <chrono>
#include <thread>

using namespace Paper;

namespace {
// Polls both sides until `done` or two seconds pass.
template <typename F> bool pump(EnetTransport &a, EnetTransport &b, std::vector<NetEvent> &ea,
                                std::vector<NetEvent> &eb, F done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    NetEvent e;
    while (a.poll(e)) ea.push_back(std::move(e));
    while (b.poll(e)) eb.push_back(std::move(e));
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}
} // namespace

TEST_CASE("ENet client and server exchange data over localhost UDP") {
  auto server = EnetTransport::createServer(0, 4);
  REQUIRE(server.has_value());
  auto client = EnetTransport::createClient();
  REQUIRE(client.has_value());
  auto peer = (*client)->connect("127.0.0.1", (*server)->port());
  REQUIRE(peer.has_value());

  std::vector<NetEvent> serverEvents;
  std::vector<NetEvent> clientEvents;
  REQUIRE(pump(**server, **client, serverEvents, clientEvents,
               [&] { return !serverEvents.empty() && !clientEvents.empty(); }));
  CHECK(serverEvents[0].type == NetEvent::Type::Connected);
  CHECK(clientEvents[0].type == NetEvent::Type::Connected);
  const PeerId clientOnServer = serverEvents[0].peer;
  serverEvents.clear();
  clientEvents.clear();

  const std::array<std::uint8_t, 3> hello{1, 2, 3};
  (*client)->send(*peer, Channel::Reliable, hello);
  (*client)->send(*peer, Channel::Unreliable, hello);
  REQUIRE(pump(**server, **client, serverEvents, clientEvents,
               [&] { return serverEvents.size() >= 2; }));
  CHECK(serverEvents[0].type == NetEvent::Type::Data);
  CHECK(serverEvents[0].data == std::vector<std::uint8_t>{1, 2, 3});

  (*server)->send(clientOnServer, Channel::Reliable, hello);
  REQUIRE(pump(**server, **client, serverEvents, clientEvents,
               [&] { return !clientEvents.empty(); }));
  CHECK(clientEvents[0].data.size() == 3);

  (*client)->disconnect(*peer);
  serverEvents.clear();
  REQUIRE(pump(**server, **client, serverEvents, clientEvents, [&] {
    return !serverEvents.empty();
  }));
  CHECK(serverEvents[0].type == NetEvent::Type::Disconnected);
}
