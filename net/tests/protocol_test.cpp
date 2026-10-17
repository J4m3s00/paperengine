#include <doctest/doctest.h>

#include "net/protocol.h"

#include <cmath>
#include <random>

using namespace Paper;

namespace {
NetPlayer makePlayer(std::uint8_t slot, float x) {
  NetPlayer p;
  p.slot = slot;
  p.state.origin = {x, 10.0F, -x};
  p.state.velocity = {1.5F * x, 0.0F, 200.0F};
  p.state.onGround = true;
  p.yaw = 0.5F;
  p.pitch = -0.2F;
  return p;
}

Snapshot makeSnapshot(std::uint32_t tick, int players) {
  Snapshot s;
  s.tick = tick;
  s.ackedCmd = 77;
  for (int i = 0; i < players; ++i) {
    s.players.push_back(makePlayer(static_cast<std::uint8_t>(i * 2), 100.0F + static_cast<float>(i)));
  }
  return s;
}

void checkSame(const Snapshot &a, const Snapshot &b) {
  CHECK(a.tick == b.tick);
  CHECK(a.ackedCmd == b.ackedCmd);
  REQUIRE(a.players.size() == b.players.size());
  for (std::size_t i = 0; i < a.players.size(); ++i) {
    CHECK(a.players[i].slot == b.players[i].slot);
    CHECK(a.players[i].state.origin == b.players[i].state.origin);
    CHECK(a.players[i].state.velocity == b.players[i].state.velocity);
    CHECK(a.players[i].state.onGround == b.players[i].state.onGround);
    CHECK(a.players[i].state.ducked == b.players[i].state.ducked);
    CHECK(a.players[i].state.jumpHeld == b.players[i].state.jumpHeld);
    CHECK(a.players[i].yaw == doctest::Approx(b.players[i].yaw).epsilon(1e-3));
    CHECK(a.players[i].pitch == doctest::Approx(b.players[i].pitch).epsilon(1e-3));
  }
}
} // namespace

TEST_CASE("bit stream round trips values of every width") {
  BitWriter w;
  w.write(5, 3);
  w.writeBool(true);
  w.write(0xDEADBEEF, 32);
  w.writeFloat(-12.25F);
  w.write(0x1FF, 9);
  BitReader r(w.bytes());
  CHECK(r.read(3) == 5);
  CHECK(r.readBool());
  CHECK(r.read(32) == 0xDEADBEEF);
  CHECK(r.readFloat() == -12.25F);
  CHECK(r.read(9) == 0x1FF);
  CHECK(r.ok());
  (void)r.read(32);
  CHECK_FALSE(r.ok());
}

TEST_CASE("handshake messages round trip") {
  const auto hello = decodeHello(encode(HelloMsg{.protocolVersion = 1, .name = "luca"}));
  REQUIRE(hello.has_value());
  CHECK(hello->name == "luca");
  const auto accept = decodeAccept(encode(AcceptMsg{3, 128, 64, 9000, "dev_world"}));
  REQUIRE(accept.has_value());
  CHECK(accept->slot == 3);
  CHECK(accept->tickRate == 128);
  CHECK(accept->serverTick == 9000);
  CHECK(accept->map == "dev_world");
  const auto reject = decodeReject(encode(RejectMsg{"server full"}));
  REQUIRE(reject.has_value());
  CHECK(reject->reason == "server full");
  CHECK_FALSE(decodeHello(encode(RejectMsg{"x"})).has_value());
}

TEST_CASE("usercmds round trip in canonical quantized form") {
  UserCmdsMsg msg;
  msg.ackedSnapshot = 1234;
  for (std::uint32_t i = 0; i < 5; ++i) {
    UserCmd c;
    c.yaw = 0.1F * static_cast<float>(i) - 3.0F;
    c.pitch = 0.3F;
    c.forward = 1.0F;
    c.side = i % 2 == 0 ? -1.0F : 0.0F;
    c.jump = i == 3;
    msg.cmds.push_back({500 + i, c});
  }
  const auto decoded = decodeUserCmds(encode(msg));
  REQUIRE(decoded.has_value());
  CHECK(decoded->ackedSnapshot == 1234);
  REQUIRE(decoded->cmds.size() == 5);
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK(decoded->cmds[i].seq == 500 + i);
    const UserCmd q = quantizeUserCmd(msg.cmds[i].cmd);
    CHECK(decoded->cmds[i].cmd.yaw == q.yaw);
    CHECK(decoded->cmds[i].cmd.forward == q.forward);
    CHECK(decoded->cmds[i].cmd.side == q.side);
    CHECK(decoded->cmds[i].cmd.jump == q.jump);
    CHECK(decoded->cmds[i].cmd.yaw == doctest::Approx(msg.cmds[i].cmd.yaw).epsilon(1e-3));
  }
  CHECK(decoded->cmds[0].cmd.forward == 1.0F);
  CHECK(decoded->cmds[1].cmd.side == 0.0F);
  CHECK(quantizeUserCmd(quantizeUserCmd(msg.cmds[2].cmd)).yaw == quantizeUserCmd(msg.cmds[2].cmd).yaw);
}

TEST_CASE("only the newest usercmds are kept when too many are sent") {
  UserCmdsMsg msg;
  for (std::uint32_t i = 0; i < 12; ++i) {
    msg.cmds.push_back({i, UserCmd{}});
  }
  const auto decoded = decodeUserCmds(encode(msg));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->cmds.size() == static_cast<std::size_t>(kMaxCmdsPerPacket));
  CHECK(decoded->cmds.back().seq == 11);
}

TEST_CASE("full snapshot round trips") {
  const Snapshot s = makeSnapshot(100, 6);
  const auto packet = encode(s, nullptr);
  CHECK(*snapshotBaselineTick(packet) == 0);
  const auto decoded = decodeSnapshot(packet, nullptr);
  REQUIRE(decoded.has_value());
  checkSame(s, *decoded);
}

TEST_CASE("delta snapshot reconstructs the new state and is smaller") {
  const Snapshot base = makeSnapshot(100, 8);
  Snapshot next = makeSnapshot(102, 8);
  next.players[3].state.origin.x += 4.0F;
  next.players[3].state.onGround = false;
  next.players.erase(next.players.begin() + 5);                    // left view
  next.players.push_back(makePlayer(31 % 16, 5.0F));              // appeared (slot 15)
  const auto full = encode(next, nullptr);
  const auto delta = encode(next, &base);
  CHECK(delta.size() < full.size());
  CHECK(*snapshotBaselineTick(delta) == 100);
  const auto decoded = decodeSnapshot(delta, &base);
  REQUIRE(decoded.has_value());
  checkSame(next, *decoded);
}

TEST_CASE("unchanged players cost almost nothing") {
  const Snapshot base = makeSnapshot(100, 10);
  Snapshot next = base;
  next.tick = 101;
  const auto delta = encode(next, &base);
  CHECK(delta.size() < 50);
}

TEST_CASE("a delta without its baseline is rejected") {
  const Snapshot base = makeSnapshot(100, 4);
  const Snapshot next = makeSnapshot(102, 4);
  const auto delta = encode(next, &base);
  CHECK_FALSE(decodeSnapshot(delta, nullptr).has_value());
  Snapshot wrong = base;
  wrong.tick = 99;
  CHECK_FALSE(decodeSnapshot(delta, &wrong).has_value());
}

TEST_CASE("truncated and random packets never crash the decoders") {
  const auto packet = encode(makeSnapshot(100, 10), nullptr);
  for (std::size_t n = 0; n < packet.size(); ++n) {
    const std::span<const std::uint8_t> cut(packet.data(), n);
    CHECK_FALSE(decodeSnapshot(cut, nullptr).has_value());
  }
  std::mt19937 rng(1234);
  const Snapshot base = makeSnapshot(100, 4);
  for (int i = 0; i < 20000; ++i) {
    std::vector<std::uint8_t> junk(rng() % 80);
    for (auto &b : junk) b = static_cast<std::uint8_t>(rng());
    if (!junk.empty()) junk[0] = static_cast<std::uint8_t>(1 + rng() % 6);
    (void)decodeSnapshot(junk, &base);
    (void)decodeUserCmds(junk);
    (void)decodeHello(junk);
    (void)decodeAccept(junk);
    (void)decodeReject(junk);
    (void)decodeEvent(junk);
    (void)peekMessageType(junk);
  }
}

TEST_CASE("combat input and view tick round trip in usercmds") {
  UserCmdsMsg msg;
  UserCmd c;
  c.attack = true;
  c.reload = true;
  c.weaponSelect = 5;
  c.viewTick = 1234.5625F;
  msg.cmds.push_back({9, c});
  const auto decoded = decodeUserCmds(encode(msg));
  REQUIRE(decoded.has_value());
  CHECK(decoded->cmds[0].cmd.attack);
  CHECK(decoded->cmds[0].cmd.reload);
  CHECK(decoded->cmds[0].cmd.weaponSelect == 5);
  CHECK(decoded->cmds[0].cmd.viewTick == 1234.5625F);
  CHECK(quantizeUserCmd(c).viewTick == 1234.5625F);
}

TEST_CASE("vitals and weapons travel in snapshots, in full and as deltas") {
  Snapshot base = makeSnapshot(100, 3);
  Snapshot next = makeSnapshot(101, 3);
  next.players[1].vitals = {37, 12, true};
  next.players[1].weapon = 5;
  next.players[2].vitals.alive = false;
  const auto full = decodeSnapshot(encode(next, nullptr), nullptr);
  REQUIRE(full.has_value());
  CHECK(full->players[1].vitals == Vitals{37, 12, true});
  CHECK(full->players[1].weapon == 5);
  const auto delta = decodeSnapshot(encode(next, &base), &base);
  REQUIRE(delta.has_value());
  CHECK(delta->players[1].vitals == Vitals{37, 12, true});
  CHECK(delta->players[1].weapon == 5);
  CHECK_FALSE(delta->players[2].vitals.alive);
  CHECK(delta->players[0].vitals == Vitals{});
}

TEST_CASE("the recipient's weapon state survives the wire") {
  Snapshot s = makeSnapshot(100, 1);
  s.ackedCmd = 5000;
  s.hasSelf = true;
  const std::int64_t now = 5000 * kSubTicksPerTick;
  s.self.weapon = 2;
  s.self.ammo[0] = {17, 63};
  s.self.ammo[6] = {0, 0};
  s.self.nextAttack = now + 3000;
  s.self.reloadEnd = now + 90000;
  s.self.drawEnd = 0;
  s.self.lastShot = now - 700;
  s.self.shotIndex = 11;
  s.self.attackHeld = true;
  s.self.extraSpread = 1.25F;
  s.self.punch = {7.5F, -2.0F};
  s.self.punchTarget = {9.0F, -2.5F};
  const auto decoded = decodeSnapshot(encode(s, nullptr), nullptr);
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->hasSelf);
  CHECK(combatEquivalent(s.self, decoded->self, 5000));
  CHECK(decoded->self.nextAttack == s.self.nextAttack);
  CHECK(decoded->self.reloadEnd == s.self.reloadEnd);
  CHECK(decoded->self.punch == s.self.punch);
  CHECK(decoded->self.punchTarget == s.self.punchTarget);
  // A weapon state that differs is noticed.
  CombatState other = decoded->self;
  other.ammo[0].clip = 16;
  CHECK_FALSE(combatEquivalent(s.self, other, 5000));
  other = decoded->self;
  other.punch.x += 0.01F;
  CHECK_FALSE(combatEquivalent(s.self, other, 5000));
}

TEST_CASE("game events round trip") {
  GameEvent e;
  e.type = GameEvent::Type::Kill;
  e.actor = 3;
  e.target = 12;
  e.weapon = 2;
  e.damage = 115;
  e.headshot = true;
  e.from = {1, 2, 3};
  e.to = {-4, 5, -6};
  const auto d = decodeEvent(encode(e));
  REQUIRE(d.has_value());
  CHECK(d->type == GameEvent::Type::Kill);
  CHECK(d->actor == 3);
  CHECK(d->target == 12);
  CHECK(d->damage == 115);
  CHECK(d->headshot);
  CHECK(d->to == glm::vec3(-4, 5, -6));
}
