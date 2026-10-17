# 0014: Weapons run on the usercmd clock; the owner gets its weapon state in every snapshot

Status: accepted

## Decision

- Weapon timing (fire rate, reload, draw, spray reset) is measured in the player's own usercmd sequence numbers, in fixed point (1/256 tick), never in server ticks. Server and client step exactly the same stream of usercmds, so ammo, recoil and spread predict exactly, even when the server skips a starved tick.
- Every snapshot carries the recipient's full weapon state as of the last executed usercmd; the client compares it with its prediction of that usercmd and replays on mismatch (the same mechanism as movement).
- Hits, damage, deaths and respawns are server-only. The client shows predicted tracers immediately and hit markers when the reliable event arrives.
- The shot's spread is seeded from the usercmd sequence and weapon, so the client can predict the exact direction the server uses.

## Consequences

- Weapon logic must stay a pure function of (state, usercmd, movement state, sequence).
- The snapshot grows by about 40 bytes per recipient; delta coding it is possible later.
- Because the seed is client-known, a cheat can precompute spread; recoil and spread are no secret in CS either.
