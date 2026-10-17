#include "game/rules.h"

namespace Paper {
RulesCvars::RulesCvars(CvarRegistry &r)
    : m_teamplay(r.addBool("mp_teamplay", false, "Team elimination rounds (off: free-for-all)")),
      m_friendlyFire(r.addBool("mp_friendlyfire", false, "Teammates can hurt each other")),
      m_freeze(r.add<float>("mp_freezetime", 5.0F, 0.0F, 60.0F, "Seconds frozen at round start")),
      m_round(r.add<float>("mp_roundtime", 115.0F, 10.0F, 600.0F, "Seconds a round lasts")),
      m_roundEnd(r.add<float>("mp_roundend_time", 5.0F, 0.0F, 30.0F, "Seconds between rounds")),
      m_matchEnd(r.add<float>("mp_matchend_time", 10.0F, 0.0F, 120.0F, "Seconds the final scoreboard stays")),
      m_winLimit(r.add<int>("mp_winlimit", 8, 1, 50, "Rounds a team needs to win the match")),
      m_respawn(r.add<float>("mp_respawn_time", 3.0F, 0.0F, 30.0F, "Seconds until respawn in free play")) {}

RulesConfig RulesCvars::rules() const {
  RulesConfig c;
  c.freezeSeconds = m_freeze.value();
  c.roundSeconds = m_round.value();
  c.roundEndSeconds = m_roundEnd.value();
  c.matchEndSeconds = m_matchEnd.value();
  c.roundsToWin = m_winLimit.value();
  return c;
}

void RoundManager::startRound(std::uint32_t tick, RulesEvents &ev) {
  m_phase = Phase::Freeze;
  m_phaseEnd = tick + ticks(m_config.freezeSeconds);
  ev.newRound = true;
}

RulesEvents RoundManager::tick(std::uint32_t tick, const TeamCounts &c) {
  RulesEvents ev;
  const auto t = static_cast<std::size_t>(Team::T);
  const auto ct = static_cast<std::size_t>(Team::CT);
  const bool bothTeams = c.total[t] > 0 && c.total[ct] > 0;

  switch (m_phase) {
  case Phase::Waiting:
    if (bothTeams) {
      startRound(tick, ev);
    }
    break;
  case Phase::Freeze:
    if (!bothTeams) {
      m_phase = Phase::Waiting;
      m_phaseEnd = 0;
    } else if (tick >= m_phaseEnd) {
      m_phase = Phase::Live;
      m_phaseEnd = tick + ticks(m_config.roundSeconds);
      ev.roundLive = true;
    }
    break;
  case Phase::Live: {
    if (!bothTeams) {
      // One side left: abandon the round without scoring it.
      m_phase = Phase::Waiting;
      m_phaseEnd = 0;
      break;
    }
    std::optional<Team> winner;
    if (c.alive[ct] == 0 && c.alive[t] > 0) {
      winner = Team::T;
    } else if (c.alive[t] == 0) {
      winner = Team::CT; // also when both are wiped out together
    } else if (tick >= m_phaseEnd) {
      winner = Team::CT; // time ran out: the defenders hold
    }
    if (winner) {
      ++m_score[static_cast<std::size_t>(*winner)];
      ev.roundWinner = winner;
      m_phase = Phase::RoundEnd;
      m_phaseEnd = tick + ticks(m_config.roundEndSeconds);
    }
    break;
  }
  case Phase::RoundEnd:
    if (tick >= m_phaseEnd) {
      for (const Team team : {Team::T, Team::CT}) {
        if (m_score[static_cast<std::size_t>(team)] >= m_config.roundsToWin) {
          m_phase = Phase::MatchEnd;
          m_phaseEnd = tick + ticks(m_config.matchEndSeconds);
          ev.matchWinner = team;
          return ev;
        }
      }
      ++m_round;
      if (bothTeams) {
        startRound(tick, ev);
      } else {
        m_phase = Phase::Waiting;
        m_phaseEnd = 0;
      }
    }
    break;
  case Phase::MatchEnd:
    if (tick >= m_phaseEnd) {
      m_score = {};
      m_round = 1;
      m_phase = Phase::Waiting;
      m_phaseEnd = 0;
      ev.newMatch = true;
    }
    break;
  }
  return ev;
}
} // namespace Paper
