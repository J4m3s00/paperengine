#include <doctest/doctest.h>

#include "net/loopback.h"

#include <array>

using namespace Paper;

namespace {
constexpr std::int64_t kMs = 1'000'000;

std::vector<NetEvent> drain(Transport &t) {
  std::vector<NetEvent> out;
  NetEvent e;
  while (t.poll(e)) {
    out.push_back(std::move(e));
  }
  return out;
}

void sendByte(Transport &t, PeerId to, Channel ch, std::uint8_t b) {
  const std::array<std::uint8_t, 1> data{b};
  t.send(to, ch, data);
}
} // namespace

TEST_CASE("connecting is seen by both sides after the latency") {
  LoopbackNetwork net;
  net.setConditions({.latencyMs = 50});
  auto &server = net.addEndpoint();
  auto &client = net.addEndpoint();
  client.connect(server.id());

  net.advance(49 * kMs);
  CHECK(drain(server).empty());
  net.advance(1 * kMs);
  auto events = drain(server);
  REQUIRE(events.size() == 1);
  CHECK(events[0].type == NetEvent::Type::Connected);
  CHECK(events[0].peer == client.id());
  events = drain(client);
  REQUIRE(events.size() == 1);
  CHECK(events[0].peer == server.id());
}

TEST_CASE("data is delayed by the latency") {
  LoopbackNetwork net;
  net.setConditions({.latencyMs = 30});
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  sendByte(a, b.id(), Channel::Unreliable, 7);
  net.advance(29 * kMs);
  CHECK(drain(b).empty());
  net.advance(1 * kMs);
  const auto events = drain(b);
  REQUIRE(events.size() == 1);
  CHECK(events[0].data == std::vector<std::uint8_t>{7});
  CHECK(events[0].peer == a.id());
}

TEST_CASE("loss drops about the configured share of unreliable packets") {
  LoopbackNetwork net(42);
  net.setConditions({.loss = 0.1});
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  for (int i = 0; i < 10000; ++i) {
    sendByte(a, b.id(), Channel::Unreliable, 0);
  }
  net.advance(1 * kMs);
  const auto received = static_cast<int>(drain(b).size());
  CHECK(received > 8800);
  CHECK(received < 9200);
  CHECK(net.packetsDropped() == static_cast<std::uint64_t>(10000 - received));
}

TEST_CASE("reliable packets are never lost and stay ordered despite jitter") {
  LoopbackNetwork net(3);
  net.setConditions({.latencyMs = 40, .jitterMs = 35, .loss = 0.5, .duplicate = 0.5});
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  for (int i = 0; i < 200; ++i) {
    sendByte(a, b.id(), Channel::Reliable, static_cast<std::uint8_t>(i));
    net.advance(1 * kMs);
  }
  net.advance(500 * kMs);
  const auto events = drain(b);
  REQUIRE(events.size() == 200);
  for (int i = 0; i < 200; ++i) {
    CHECK(events[static_cast<std::size_t>(i)].data[0] == static_cast<std::uint8_t>(i));
  }
}

TEST_CASE("jitter reorders unreliable packets") {
  LoopbackNetwork net(5);
  net.setConditions({.latencyMs = 50, .jitterMs = 40});
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  for (int i = 0; i < 100; ++i) {
    sendByte(a, b.id(), Channel::Unreliable, static_cast<std::uint8_t>(i));
    net.advance(1 * kMs);
  }
  net.advance(200 * kMs);
  const auto events = drain(b);
  REQUIRE(events.size() == 100);
  int inversions = 0;
  for (std::size_t i = 1; i < events.size(); ++i) {
    inversions += events[i].data[0] < events[i - 1].data[0] ? 1 : 0;
  }
  CHECK(inversions > 5);
}

TEST_CASE("duplication delivers extra copies") {
  LoopbackNetwork net(9);
  net.setConditions({.duplicate = 1.0});
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  sendByte(a, b.id(), Channel::Unreliable, 1);
  net.advance(1 * kMs);
  CHECK(drain(b).size() == 2);
}

TEST_CASE("the same seed gives the same delivery") {
  const auto run = [](std::uint64_t seed) {
    LoopbackNetwork net(seed);
    net.setConditions({.latencyMs = 20, .jitterMs = 15, .loss = 0.2, .duplicate = 0.1});
    auto &a = net.addEndpoint();
    auto &b = net.addEndpoint();
    std::vector<std::uint8_t> order;
    for (int i = 0; i < 300; ++i) {
      sendByte(a, b.id(), Channel::Unreliable, static_cast<std::uint8_t>(i));
      net.advance(2 * kMs);
      for (auto &e : drain(b)) {
        order.push_back(e.data[0]);
      }
    }
    return order;
  };
  CHECK(run(11) == run(11));
  CHECK(run(11) != run(12));
}

TEST_CASE("disconnect notifies both sides") {
  LoopbackNetwork net;
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  a.disconnect(b.id());
  net.advance(1 * kMs);
  CHECK(drain(a)[0].type == NetEvent::Type::Disconnected);
  CHECK(drain(b)[0].type == NetEvent::Type::Disconnected);
}

#include "net/conditioned_transport.h"

TEST_CASE("conditioned transport delays both directions") {
  LoopbackNetwork net;
  auto &a = net.addEndpoint();
  auto &b = net.addEndpoint();
  ConditionedTransport wrapped(a, {.latencyMs = 40});
  std::int64_t now = 0;
  const auto step = [&](std::int64_t ns) {
    now += ns;
    net.advance(ns);
    wrapped.update(now);
  };
  sendByte(wrapped, b.id(), Channel::Unreliable, 1);
  step(1 * kMs); // handed to the wire only after 40 ms
  CHECK(drain(b).empty());
  step(39 * kMs);
  net.advance(0);
  CHECK(drain(b).size() == 1);

  sendByte(b, a.id(), Channel::Unreliable, 2);
  step(1 * kMs);
  CHECK(drain(wrapped).empty()); // arrived at `a`, held back 40 ms
  step(40 * kMs);
  CHECK(drain(wrapped).size() == 1);
}
