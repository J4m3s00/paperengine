# Game plan: a Counter-Strike-style FPS on Paper Engine

Goal: turn the engine into a competitive-feel FPS. Movement, gunplay and netcode are the product. Performance is a requirement, not a polish step.

## Targets

- Platforms: Windows, Linux, macOS (MoltenVK). Dedicated server on Linux (and Windows).
- Performance: 240 FPS at 1080p on a GTX 1660 / RX 580 class GPU (Windows/Linux), 144+ FPS at 1080p on an Apple M1 base chip. 1% lows and input-to-photon latency matter more than average FPS.
- Server: 10 players (16 slots) at 128 Hz tick, allocation-free tick, many instances per core.
- Maps up to ~200k triangles.

## Decisions

ADR 0009 (netcode), 0010 (collision, movement, floats), 0011 (hitboxes), 0012 (targets). Also chosen without an ADR: ENet, Tracy, Dear ImGui, miniaudio, doctest (all via FetchContent); free placeholder assets; direct connect only; weapons are data-driven (JSON) and easy to extend (AK-47, M4A4, AWP, Glock-18, USP-S, Deagle, knife for the slice).

## Milestones

Time budget is about 10 hours per week with no deadline, so sizes are relative (S ≈ 1-2 weeks, M ≈ 3-5, L ≈ 6+ at that pace).

| | Milestone | Size | Exit gate |
|---|---|---|---|
| [M0](M0-foundations.md) | Foundations | M | Targets split, fixed-timestep loop, tests, profiler, debug UI |
| [M1](M1-movement-sandbox.md) | Movement sandbox (single player) | L | Movement matches CS:GO reference; replay tests pass |
| [M2](M2-networking.md) | Networking core | L | Smooth at 100 ms RTT, 2% loss; bot harness soak test |
| [M3](M3-combat.md) | Combat in the dev world | L | Shoot stationary bots with lag compensation; spray patterns |
| [M4](M4-game-rules.md) | Game rules | M | Rounds, teams, defusal or deathmatch, scoreboard, menu |
| [M5](M5-editor.md) | Editor and map compiler | L | A map built in the editor plays in the game |
| [M6](M6-polish-and-perf.md) | Polish and performance | L | Skeletal animation, culling, final perf targets met |

M0-M3 together are the "dev world": one explorable map with stationary bots to practise movement and shooting.

## Cross-cutting rules

- The simulation (`game`) is headless and replayable: a recorded usercmd stream must reproduce the same end state. Every movement or weapon change keeps replay tests green.
- Every milestone ends with measured numbers (frame time, tick time, bandwidth), not impressions.
- New protocol fields and gameplay state go through the shared `game`/`net` code, never client-only paths.

## Open questions

- Bomb defusal vs. deathmatch for the first real mode: decided in M4 (ADR 0015): team elimination first, bomb later.
- When to move from fixed hitboxes to skeleton hitboxes (decided from M6 profiling data).
- Whether sub-tick input timestamps are needed once the 128 Hz tick is measured (M2).
