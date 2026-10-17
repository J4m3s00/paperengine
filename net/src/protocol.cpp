#include "net/protocol.h"

#include <cmath>
#include <numbers>

namespace Paper {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kStringMaxLength = 63;

Error bad(const char *what) { return {ErrorCode::Network, std::string("malformed packet: ") + what}; }

void writeAngle(BitWriter &w, float radians) {
  const float wrapped = radians - 2.0F * kPi * std::floor((radians + kPi) / (2.0F * kPi));
  w.writeQuantized(wrapped, -kPi, kPi, 16);
}
float readAngle(BitReader &r) { return r.readQuantized(-kPi, kPi, 16); }

void writeAxis(BitWriter &w, float v) { w.writeQuantized(v, -1.0F, 1.0F, 8); }
float readAxis(BitReader &r) {
  // 8 bits over [-1, 1] has no exact zero; snap the middle steps to it.
  const float v = r.readQuantized(-1.0F, 1.0F, 8);
  return std::fabs(v) < 0.004F ? 0.0F : v;
}

void writeString(BitWriter &w, const std::string &s) {
  const int n = static_cast<int>(std::min<std::size_t>(s.size(), kStringMaxLength));
  w.write(static_cast<std::uint32_t>(n), 6);
  for (int i = 0; i < n; ++i) {
    w.write(static_cast<unsigned char>(s[static_cast<std::size_t>(i)]), 8);
  }
}
std::string readString(BitReader &r) {
  const int n = static_cast<int>(r.read(6));
  std::string s;
  for (int i = 0; i < n; ++i) {
    s.push_back(static_cast<char>(r.read(8)));
  }
  return s;
}

void writeCmd(BitWriter &w, const UserCmd &c) {
  writeAngle(w, c.yaw);
  writeAngle(w, c.pitch);
  writeAxis(w, c.forward);
  writeAxis(w, c.side);
  w.writeBool(c.jump);
  w.writeBool(c.duck);
  w.writeBool(c.walk);
}
UserCmd readCmd(BitReader &r) {
  UserCmd c;
  c.yaw = readAngle(r);
  c.pitch = readAngle(r);
  c.forward = readAxis(r);
  c.side = readAxis(r);
  c.jump = r.readBool();
  c.duck = r.readBool();
  c.walk = r.readBool();
  return c;
}

BitReader open(std::span<const std::uint8_t> packet, MessageType expected, bool &typeOk) {
  BitReader r(packet);
  typeOk = r.read(8) == static_cast<std::uint32_t>(expected);
  return r;
}

void writePlayerFull(BitWriter &w, const NetPlayer &p) {
  w.writeFloat(p.state.origin.x);
  w.writeFloat(p.state.origin.y);
  w.writeFloat(p.state.origin.z);
  w.writeFloat(p.state.velocity.x);
  w.writeFloat(p.state.velocity.y);
  w.writeFloat(p.state.velocity.z);
  w.writeBool(p.state.onGround);
  w.writeBool(p.state.ducked);
  w.writeBool(p.state.jumpHeld);
  writeAngle(w, p.yaw);
  writeAngle(w, p.pitch);
}
void readPlayerFull(BitReader &r, NetPlayer &p) {
  p.state.origin = {r.readFloat(), r.readFloat(), r.readFloat()};
  p.state.velocity = {r.readFloat(), r.readFloat(), r.readFloat()};
  p.state.onGround = r.readBool();
  p.state.ducked = r.readBool();
  p.state.jumpHeld = r.readBool();
  p.yaw = readAngle(r);
  p.pitch = readAngle(r);
}

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }
std::uint32_t flagBits(const PlayerState &s) {
  return (s.onGround ? 1U : 0U) | (s.ducked ? 2U : 0U) | (s.jumpHeld ? 4U : 0U);
}
// Angles are compared after quantization: the receiver can only see those.
std::uint32_t angleBits(float a) {
  BitWriter w;
  writeAngle(w, a);
  return static_cast<std::uint32_t>(w.bytes()[0]) | (static_cast<std::uint32_t>(w.bytes()[1]) << 8);
}

void writePlayerDelta(BitWriter &w, const NetPlayer &p, const NetPlayer &b) {
  const float now[6] = {p.state.origin.x, p.state.origin.y, p.state.origin.z,
                        p.state.velocity.x, p.state.velocity.y, p.state.velocity.z};
  const float was[6] = {b.state.origin.x, b.state.origin.y, b.state.origin.z,
                        b.state.velocity.x, b.state.velocity.y, b.state.velocity.z};
  for (int i = 0; i < 6; ++i) {
    const bool changed = bits(now[i]) != bits(was[i]);
    w.writeBool(changed);
    if (changed) {
      w.writeFloat(now[i]);
    }
  }
  const bool flagsChanged = flagBits(p.state) != flagBits(b.state);
  w.writeBool(flagsChanged);
  if (flagsChanged) {
    w.write(flagBits(p.state), 3);
  }
  const bool yawChanged = angleBits(p.yaw) != angleBits(b.yaw);
  w.writeBool(yawChanged);
  if (yawChanged) {
    writeAngle(w, p.yaw);
  }
  const bool pitchChanged = angleBits(p.pitch) != angleBits(b.pitch);
  w.writeBool(pitchChanged);
  if (pitchChanged) {
    writeAngle(w, p.pitch);
  }
}
void readPlayerDelta(BitReader &r, NetPlayer &p) {
  float *fields[6] = {&p.state.origin.x, &p.state.origin.y, &p.state.origin.z,
                      &p.state.velocity.x, &p.state.velocity.y, &p.state.velocity.z};
  for (float *f : fields) {
    if (r.readBool()) {
      *f = r.readFloat();
    }
  }
  if (r.readBool()) {
    const std::uint32_t f = r.read(3);
    p.state.onGround = (f & 1U) != 0;
    p.state.ducked = (f & 2U) != 0;
    p.state.jumpHeld = (f & 4U) != 0;
  }
  if (r.readBool()) {
    p.yaw = readAngle(r);
  }
  if (r.readBool()) {
    p.pitch = readAngle(r);
  }
}

const NetPlayer *findPlayer(const Snapshot &s, std::uint8_t slot) {
  for (const NetPlayer &p : s.players) {
    if (p.slot == slot) {
      return &p;
    }
  }
  return nullptr;
}
} // namespace

std::expected<MessageType, Error> peekMessageType(std::span<const std::uint8_t> packet) {
  if (packet.empty() || packet[0] < 1 || packet[0] > static_cast<std::uint8_t>(MessageType::Snapshot)) {
    return std::unexpected(bad("unknown message type"));
  }
  return static_cast<MessageType>(packet[0]);
}

UserCmd quantizeUserCmd(const UserCmd &cmd) {
  BitWriter w;
  writeCmd(w, cmd);
  const auto bytes = w.take();
  BitReader r(bytes);
  return readCmd(r);
}

std::vector<std::uint8_t> encode(const HelloMsg &msg) {
  BitWriter w;
  w.write(static_cast<std::uint32_t>(MessageType::Hello), 8);
  w.write(msg.protocolVersion, 16);
  writeString(w, msg.name);
  return w.take();
}
std::vector<std::uint8_t> encode(const AcceptMsg &msg) {
  BitWriter w;
  w.write(static_cast<std::uint32_t>(MessageType::Accept), 8);
  w.write(msg.slot, 8);
  w.write(msg.tickRate, 16);
  w.write(msg.snapshotRate, 16);
  w.write(msg.serverTick, 32);
  writeString(w, msg.map);
  return w.take();
}
std::vector<std::uint8_t> encode(const RejectMsg &msg) {
  BitWriter w;
  w.write(static_cast<std::uint32_t>(MessageType::Reject), 8);
  writeString(w, msg.reason);
  return w.take();
}
std::vector<std::uint8_t> encode(const UserCmdsMsg &msg) {
  BitWriter w;
  w.write(static_cast<std::uint32_t>(MessageType::UserCmds), 8);
  w.write(msg.ackedSnapshot, 32);
  const auto count = std::min<std::size_t>(msg.cmds.size(), kMaxCmdsPerPacket);
  w.write(static_cast<std::uint32_t>(count), 4);
  if (count > 0) {
    // Only the first sequence number is sent; the rest are consecutive.
    const std::size_t first = msg.cmds.size() - count;
    w.write(msg.cmds[first].seq, 32);
    for (std::size_t i = first; i < msg.cmds.size(); ++i) {
      writeCmd(w, msg.cmds[i].cmd);
    }
  }
  return w.take();
}

std::vector<std::uint8_t> encode(const Snapshot &snapshot, const Snapshot *baseline) {
  BitWriter w;
  w.write(static_cast<std::uint32_t>(MessageType::Snapshot), 8);
  w.write(snapshot.tick, 32);
  w.write(baseline ? baseline->tick : 0U, 32);
  w.write(snapshot.ackedCmd, 32);
  w.write(static_cast<std::uint32_t>(snapshot.players.size()), 5);
  for (const NetPlayer &p : snapshot.players) {
    w.write(p.slot, 5);
    const NetPlayer *old = baseline ? findPlayer(*baseline, p.slot) : nullptr;
    if (old) {
      w.writeBool(true);
      writePlayerDelta(w, p, *old);
    } else {
      w.writeBool(false);
      writePlayerFull(w, p);
    }
  }
  return w.take();
}

std::expected<HelloMsg, Error> decodeHello(std::span<const std::uint8_t> packet) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::Hello, typeOk);
  HelloMsg m;
  m.protocolVersion = static_cast<std::uint16_t>(r.read(16));
  m.name = readString(r);
  if (!typeOk || !r.ok()) {
    return std::unexpected(bad("hello"));
  }
  return m;
}
std::expected<AcceptMsg, Error> decodeAccept(std::span<const std::uint8_t> packet) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::Accept, typeOk);
  AcceptMsg m;
  m.slot = static_cast<std::uint8_t>(r.read(8));
  m.tickRate = static_cast<std::uint16_t>(r.read(16));
  m.snapshotRate = static_cast<std::uint16_t>(r.read(16));
  m.serverTick = r.read(32);
  m.map = readString(r);
  if (!typeOk || !r.ok() || m.slot >= kMaxPlayerSlots) {
    return std::unexpected(bad("accept"));
  }
  return m;
}
std::expected<RejectMsg, Error> decodeReject(std::span<const std::uint8_t> packet) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::Reject, typeOk);
  RejectMsg m;
  m.reason = readString(r);
  if (!typeOk || !r.ok()) {
    return std::unexpected(bad("reject"));
  }
  return m;
}
std::expected<UserCmdsMsg, Error> decodeUserCmds(std::span<const std::uint8_t> packet) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::UserCmds, typeOk);
  UserCmdsMsg m;
  m.ackedSnapshot = r.read(32);
  const std::uint32_t count = r.read(4);
  if (count > static_cast<std::uint32_t>(kMaxCmdsPerPacket)) {
    return std::unexpected(bad("too many usercmds"));
  }
  if (count > 0) {
    const std::uint32_t first = r.read(32);
    for (std::uint32_t i = 0; i < count; ++i) {
      m.cmds.push_back({first + i, readCmd(r)});
    }
  }
  if (!typeOk || !r.ok()) {
    return std::unexpected(bad("usercmds"));
  }
  return m;
}

std::expected<std::uint32_t, Error> snapshotBaselineTick(std::span<const std::uint8_t> packet) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::Snapshot, typeOk);
  (void)r.read(32);
  const std::uint32_t baseline = r.read(32);
  if (!typeOk || !r.ok()) {
    return std::unexpected(bad("snapshot header"));
  }
  return baseline;
}

std::expected<Snapshot, Error> decodeSnapshot(std::span<const std::uint8_t> packet,
                                              const Snapshot *baseline) {
  bool typeOk = false;
  BitReader r = open(packet, MessageType::Snapshot, typeOk);
  Snapshot s;
  s.tick = r.read(32);
  const std::uint32_t baselineTick = r.read(32);
  s.ackedCmd = r.read(32);
  const std::uint32_t count = r.read(5);
  if (!typeOk || !r.ok()) {
    return std::unexpected(bad("snapshot header"));
  }
  if (baselineTick != 0 && (!baseline || baseline->tick != baselineTick)) {
    return std::unexpected(bad("snapshot baseline missing"));
  }
  if (count > kMaxPlayerSlots) {
    return std::unexpected(bad("too many players"));
  }
  int lastSlot = -1;
  for (std::uint32_t i = 0; i < count; ++i) {
    NetPlayer p;
    p.slot = static_cast<std::uint8_t>(r.read(5));
    if (p.slot >= kMaxPlayerSlots || static_cast<int>(p.slot) <= lastSlot) {
      return std::unexpected(bad("player slots out of order"));
    }
    lastSlot = p.slot;
    if (r.readBool()) {
      const NetPlayer *old = baselineTick != 0 ? findPlayer(*baseline, p.slot) : nullptr;
      if (!old) {
        return std::unexpected(bad("delta against a missing player"));
      }
      p = *old;
      readPlayerDelta(r, p);
    } else {
      readPlayerFull(r, p);
    }
    s.players.push_back(p);
  }
  if (!r.ok()) {
    return std::unexpected(bad("snapshot truncated"));
  }
  return s;
}
} // namespace Paper
