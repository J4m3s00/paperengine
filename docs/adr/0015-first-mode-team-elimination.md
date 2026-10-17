# 0015: First game mode is team elimination

Status: accepted

## Decision

The first real mode is round-based team elimination (T vs CT, no bomb): freeze time, live round, round ends when a team is wiped out or the time runs out (CT win), first to N rounds wins the match. Deathmatch stays as the free-for-all fallback when `mp_teamplay` is off. Bomb defusal, buy phase and economy come later.

## Why

- It exercises everything the vertical slice is about (movement, gunplay, netcode, lag compensation, round flow) with the smallest amount of new rules code.
- Defusal needs bomb entities, planting, defusing, sites and an economy to make sense; none of that tests the core better, and all of it can sit on top of the same `RoundManager`.
- Deathmatch alone has no round structure to stress state synchronisation (respawn waves, freeze, phase changes during prediction).

## Consequences

- Rules are a pure state machine (`game/rules.h`) so a bomb mode can extend it with another win condition.
- Phase restrictions (freeze) are part of the predicted simulation; the client uses the phase from the newest snapshot, which costs a couple of small corrections at phase boundaries.
