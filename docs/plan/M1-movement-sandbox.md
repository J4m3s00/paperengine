# M1: Movement sandbox (size L)

Single-player Source-style movement in a test map. This is the most important feel milestone.

## Scope

- `game` collision module (ADR 0010): hull traces against convex brushes and a triangle BVH, allocation-free, with unit tests.
- Test map format: text/JSON (brushes, spawn points, entities) and a loader. Hand-written first map with stairs, ramps, surf ramps, slopes, ledges, gaps for bhop.
- Player movement: ground move, friction, gravity, jump, crouch, air accelerate, ramp slide, step-up, duck mechanics, all constants as cvars (sv_accelerate, sv_airaccelerate, sv_friction, sv_gravity, ...).
- First-person camera and view bob-free rendering; the sandbox/client drives usercmds into the sim.
- Replay: record a usercmd stream, replay it headless, assert end state. Regression tests for bhop, air-strafe, stairs, ramps.
- Movement debug HUD: speed, ground state, last-trace visualisation.

## Exit gate

- Movement reproduces CS:GO reference behaviour on the numbers we choose (max ground speed, bhop gain, air-strafe gain, jump height, step height) and feels right to the developer.
- Replay tests pass and run in CI-friendly time.
- Sim tick cost is measured and recorded (target: far below 1% of the 7.8 ms tick budget per player).

## Status: implemented (issues #16-#21)

Measured on an Apple M4, release build:

- Hull trace over 2000 brushes: ~230 ns. `movePlayer`: ~132 ns per player per tick (0.002% of the 7.8 ms tick budget).
- Ground speed reaches 249 u/s after ~0.55 s (friction makes the last units creep, as in Source); jump rises ~57 units; strafe-hopping gains past the 275 u/s jump cap; steep ramps (normal.y < 0.7) cannot be stood on.
- Known gaps: edge friction, crouch transition timing, ladders, water, triangle-mesh collision. Interactive feel (mouse, keyboard) has not been tuned yet; the numbers above come from tests and replays.
