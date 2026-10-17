#include <doctest/doctest.h>

#include "game/rules.h"

using namespace Paper;

namespace {
constexpr std::uint32_t kRate = 100; // easy numbers: 1 s = 100 ticks

RulesConfig fast() {
  RulesConfig c;
  c.freezeSeconds = 1.0F;
  c.roundSeconds = 10.0F;
  c.roundEndSeconds = 1.0F;
  c.matchEndSeconds = 2.0F;
  c.roundsToWin = 2;
  return c;
}

TeamCounts counts(int t, int ct, int aliveT, int aliveCt) {
  TeamCounts c;
  c.total[1] = t;
  c.total[2] = ct;
  c.alive[1] = aliveT;
  c.alive[2] = aliveCt;
  return c;
}

// Drives the manager with fixed counts until `done` or `maxTicks`.
struct Sim {
  RoundManager rules{fast(), kRate};
  std::uint32_t now = 0;
  TeamCounts c = counts(2, 2, 2, 2);

  RulesEvents step() { return rules.tick(++now, c); }
  RulesEvents until(Phase phase, std::uint32_t maxTicks = 5000) {
    RulesEvents last;
    for (std::uint32_t i = 0; i < maxTicks && rules.phase() != phase; ++i) {
      last = step();
    }
    REQUIRE(rules.phase() == phase);
    return last;
  }
};
} // namespace

TEST_CASE("waiting until both teams have players") {
  Sim s;
  s.c = counts(2, 0, 2, 0);
  for (int i = 0; i < 50; ++i) s.step();
  CHECK(s.rules.phase() == Phase::Waiting);
  s.c = counts(2, 1, 2, 1);
  const RulesEvents ev = s.step();
  CHECK(ev.newRound);
  CHECK(s.rules.phase() == Phase::Freeze);
}

TEST_CASE("freeze ends after the freeze time and the round goes live") {
  Sim s;
  s.step();
  REQUIRE(s.rules.phase() == Phase::Freeze);
  const std::uint32_t start = s.now;
  const RulesEvents ev = s.until(Phase::Live);
  CHECK(ev.roundLive);
  CHECK(s.now - start == 100);
  CHECK(s.rules.phaseEndTick() == s.now + 1000);
}

TEST_CASE("wiping out a team ends the round for the other") {
  Sim s;
  s.until(Phase::Live);
  s.c = counts(2, 2, 2, 0);
  const RulesEvents ev = s.step();
  REQUIRE(ev.roundWinner.has_value());
  CHECK(*ev.roundWinner == Team::T);
  CHECK(s.rules.score(Team::T) == 1);
  CHECK(s.rules.phase() == Phase::RoundEnd);
}

TEST_CASE("time running out is a win for the counter-terrorists") {
  Sim s;
  s.until(Phase::Live);
  const RulesEvents ev = s.until(Phase::RoundEnd);
  CHECK(*ev.roundWinner == Team::CT);
  CHECK(s.rules.score(Team::CT) == 1);
}

TEST_CASE("both teams dying together is not a win for the attackers") {
  Sim s;
  s.until(Phase::Live);
  s.c = counts(2, 2, 0, 0);
  CHECK(*s.step().roundWinner == Team::CT);
}

TEST_CASE("the next round starts after the round end pause") {
  Sim s;
  s.until(Phase::Live);
  s.c = counts(2, 2, 2, 0);
  s.step();
  s.c = counts(2, 2, 2, 2);
  const RulesEvents ev = s.until(Phase::Freeze);
  CHECK(ev.newRound);
  CHECK(s.rules.round() == 2);
}

TEST_CASE("the first team to the win limit wins the match, then it restarts") {
  Sim s;
  for (int round = 0; round < 2; ++round) {
    s.until(Phase::Live);
    s.c = counts(2, 2, 2, 0); // T wins
    s.step();
    s.c = counts(2, 2, 2, 2);
    if (round == 0) s.until(Phase::Freeze);
  }
  const RulesEvents end = s.until(Phase::MatchEnd);
  REQUIRE(end.matchWinner.has_value());
  CHECK(*end.matchWinner == Team::T);
  CHECK(s.rules.score(Team::T) == 2);
  const RulesEvents restart = s.until(Phase::Waiting);
  CHECK(restart.newMatch);
  CHECK(s.rules.score(Team::T) == 0);
  CHECK(s.rules.round() == 1);
  s.step();
  CHECK(s.rules.phase() == Phase::Freeze);
}

TEST_CASE("a team leaving mid round cancels it without scoring") {
  Sim s;
  s.until(Phase::Live);
  s.c = counts(2, 0, 2, 0);
  s.step();
  CHECK(s.rules.phase() == Phase::Waiting);
  CHECK(s.rules.score(Team::T) == 0);
  CHECK(s.rules.score(Team::CT) == 0);
}

TEST_CASE("a team leaving during freeze goes back to waiting") {
  Sim s;
  s.step();
  s.c = counts(0, 2, 0, 2);
  s.step();
  CHECK(s.rules.phase() == Phase::Waiting);
}
