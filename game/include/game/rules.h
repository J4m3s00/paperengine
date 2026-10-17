#pragma once

#include "base/cvar.h"

#include <array>
#include <cstdint>
#include <optional>

// Game rules of the first mode, team elimination: two teams, rounds with a
// freeze time, a live phase that ends when a team is wiped out or time runs
// out, and a match won by the first team to a number of rounds. Pure logic,
// stepped once per server tick, so it is testable without a server.
namespace Paper {
enum class Team : std::uint8_t { None = 0, T = 1, CT = 2 };

[[nodiscard]] inline Team opponent(Team t) { return t == Team::T ? Team::CT : Team::T; }

enum class Phase : std::uint8_t {
  // Not enough players for both teams: free play, players respawn.
  Waiting,
  // Round starts, everyone is placed at their spawn and cannot move or shoot.
  Freeze,
  Live,
  // Short pause showing who won the round.
  RoundEnd,
  // Final scoreboard before the next match.
  MatchEnd,
};

struct RulesConfig {
  float freezeSeconds = 5.0F;
  float roundSeconds = 115.0F;
  float roundEndSeconds = 5.0F;
  float matchEndSeconds = 10.0F;
  // Rounds a team needs to win the match.
  int roundsToWin = 8;
};

// What the server tells the rules each tick.
struct TeamCounts {
  int total[3] = {0, 0, 0};
  int alive[3] = {0, 0, 0};
};

struct RulesEvents {
  // Respawn everyone and start the freeze.
  bool newRound = false;
  // Freeze is over, shooting starts.
  bool roundLive = false;
  std::optional<Team> roundWinner;
  std::optional<Team> matchWinner;
  // The match restarted from zero (scores were reset).
  bool newMatch = false;
};

// What players may do in a phase (the server enforces it, the client predicts
// the same).
[[nodiscard]] inline bool phaseAllowsMovement(Phase p) { return p != Phase::Freeze; }
[[nodiscard]] inline bool phaseAllowsShooting(Phase p) {
  return p == Phase::Waiting || p == Phase::Live;
}

// What everyone sees of the match; replicated in every snapshot.
struct MatchState {
  Phase phase = Phase::Waiting;
  std::uint8_t round = 1;
  std::uint8_t scoreT = 0;
  std::uint8_t scoreCT = 0;
  // Server tick at which the phase ends (0 = no end).
  std::uint32_t phaseEndTick = 0;
  bool operator==(const MatchState &) const = default;
};

struct PlayerInfo {
  Team team = Team::None;
  std::uint8_t kills = 0;
  std::uint8_t deaths = 0;
  bool operator==(const PlayerInfo &) const = default;
};

// cvars for a team-play server: mp_teamplay, mp_freezetime, mp_roundtime,
// mp_roundend_time, mp_matchend_time, mp_winlimit, mp_friendlyfire,
// mp_respawn_time.
class RulesCvars {
public:
  explicit RulesCvars(CvarRegistry &registry = cvars());
  [[nodiscard]] RulesConfig rules() const;
  [[nodiscard]] bool teamplay() const { return m_teamplay.value(); }
  [[nodiscard]] bool friendlyFire() const { return m_friendlyFire.value(); }
  [[nodiscard]] float respawnSeconds() const { return m_respawn.value(); }

private:
  Cvar<bool> &m_teamplay;
  Cvar<bool> &m_friendlyFire;
  Cvar<float> &m_freeze;
  Cvar<float> &m_round;
  Cvar<float> &m_roundEnd;
  Cvar<float> &m_matchEnd;
  Cvar<int> &m_winLimit;
  Cvar<float> &m_respawn;
};

class RoundManager {
public:
  RoundManager(const RulesConfig &config, std::uint32_t tickRate)
      : m_config(config), m_tickRate(tickRate) {}

  RulesEvents tick(std::uint32_t tick, const TeamCounts &counts);

  [[nodiscard]] Phase phase() const { return m_phase; }
  // The tick at which the current phase ends (0 in Waiting).
  [[nodiscard]] std::uint32_t phaseEndTick() const { return m_phaseEnd; }
  [[nodiscard]] int round() const { return m_round; }
  [[nodiscard]] int score(Team team) const { return m_score[static_cast<std::size_t>(team)]; }
  [[nodiscard]] const RulesConfig &config() const { return m_config; }
  [[nodiscard]] MatchState matchState() const {
    return {m_phase, static_cast<std::uint8_t>(m_round), static_cast<std::uint8_t>(score(Team::T)),
            static_cast<std::uint8_t>(score(Team::CT)), m_phaseEnd};
  }

private:
  [[nodiscard]] std::uint32_t ticks(float seconds) const {
    return static_cast<std::uint32_t>(seconds * static_cast<float>(m_tickRate));
  }
  void startRound(std::uint32_t tick, RulesEvents &ev);

  RulesConfig m_config;
  std::uint32_t m_tickRate;
  Phase m_phase = Phase::Waiting;
  std::uint32_t m_phaseEnd = 0;
  int m_round = 1;
  std::array<int, 3> m_score{};
};
} // namespace Paper
