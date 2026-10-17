#pragma once

#include "base/error.h"
#include "game/movement.h"
#include "net/bitstream.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

// Wire protocol (ADR 0009). Every packet starts with a MessageType byte.
// Hello / Accept / Reject travel on the reliable channel, UserCmds and
// Snapshot on the unreliable one.
namespace Paper {
inline constexpr std::uint16_t kDefaultServerPort = 27015;
inline constexpr std::uint32_t kMaxPlayerSlots = 16;
inline constexpr std::uint16_t kProtocolVersion = 1;
// Most usercmds in one packet (the newest plus recent ones as loss cover).
inline constexpr int kMaxCmdsPerPacket = 8;

enum class MessageType : std::uint8_t { Hello = 1, Accept, Reject, UserCmds, Snapshot };

// Reads the type byte of a packet without consuming it.
[[nodiscard]] std::expected<MessageType, Error> peekMessageType(std::span<const std::uint8_t> packet);

struct HelloMsg {
  std::uint16_t protocolVersion = kProtocolVersion;
  std::string name;
};
struct AcceptMsg {
  std::uint8_t slot = 0;
  std::uint16_t tickRate = 0;
  std::uint16_t snapshotRate = 0;
  std::uint32_t serverTick = 0;
  std::string map;
};
struct RejectMsg {
  std::string reason;
};

// A usercmd with its client-chosen sequence number. Sequence numbers of one
// client are consecutive; the server executes them in order, one per tick.
struct NetUserCmd {
  std::uint32_t seq = 0;
  UserCmd cmd;
};

// The input as the wire carries it (16 bit angles, 8 bit axes). The client
// predicts with this canonical form so it matches what the server runs.
[[nodiscard]] UserCmd quantizeUserCmd(const UserCmd &cmd);

struct UserCmdsMsg {
  // Newest snapshot tick the client has received; the server uses it as the
  // delta baseline for what it sends next.
  std::uint32_t ackedSnapshot = 0;
  // Oldest first, consecutive sequence numbers, at most kMaxCmdsPerPacket.
  std::vector<NetUserCmd> cmds;
};

// One replicated player.
struct NetPlayer {
  std::uint8_t slot = 0;
  PlayerState state;
  float yaw = 0.0F;
  float pitch = 0.0F;
};

struct Snapshot {
  std::uint32_t tick = 0;
  // Last usercmd sequence of the recipient the server has executed.
  std::uint32_t ackedCmd = 0;
  // Usercmds the server has queued for the recipient (clock sync input).
  std::uint8_t cmdQueue = 0;
  // Ascending by slot. Only what this recipient may see.
  std::vector<NetPlayer> players;
};

[[nodiscard]] std::vector<std::uint8_t> encode(const HelloMsg &msg);
[[nodiscard]] std::vector<std::uint8_t> encode(const AcceptMsg &msg);
[[nodiscard]] std::vector<std::uint8_t> encode(const RejectMsg &msg);
[[nodiscard]] std::vector<std::uint8_t> encode(const UserCmdsMsg &msg);
// `baseline` is the last snapshot the recipient acknowledged, or null for a
// full snapshot. The wire records the baseline tick.
[[nodiscard]] std::vector<std::uint8_t> encode(const Snapshot &snapshot, const Snapshot *baseline);

[[nodiscard]] std::expected<HelloMsg, Error> decodeHello(std::span<const std::uint8_t> packet);
[[nodiscard]] std::expected<AcceptMsg, Error> decodeAccept(std::span<const std::uint8_t> packet);
[[nodiscard]] std::expected<RejectMsg, Error> decodeReject(std::span<const std::uint8_t> packet);
[[nodiscard]] std::expected<UserCmdsMsg, Error> decodeUserCmds(std::span<const std::uint8_t> packet);

// Baseline tick a snapshot packet is relative to (0 = full), so the receiver
// can pick the right baseline before decoding.
[[nodiscard]] std::expected<std::uint32_t, Error>
snapshotBaselineTick(std::span<const std::uint8_t> packet);
// Fails if the packet is malformed or needs a baseline that is missing/wrong.
[[nodiscard]] std::expected<Snapshot, Error> decodeSnapshot(std::span<const std::uint8_t> packet,
                                                            const Snapshot *baseline);
} // namespace Paper
