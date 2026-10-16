# 0012: Target split and shared simulation

Status: accepted

## Decision

`core` stays the engine (window, Vulkan renderer, scene, assets). New targets:

- `game`: shared deterministic gameplay (movement, collision, weapons, rules, entity state). No rendering, windowing or networking. Compiled into both client and server.
- `net`: protocol, snapshots, ENet transport, network conditioner.
- `client`: window, renderer, input, prediction, interpolation, UI, audio.
- `server`: headless executable. Must build and run without Vulkan or GLFW.
- `editor`: in-engine level editor (ImGui) and the map compiler.

Dependency direction: `client` -> {`core`, `game`, `net`}, `server` -> {`game`, `net`}, `editor` -> {`core`, `game`}. `game` depends on neither `core`'s renderer nor `net`. The `sandbox` target remains as the engine test bed.

## Consequences

- `core` pieces the server needs (math, error type, logging) must not pull in Vulkan or GLFW; the plan splits them out in M0.
- The same movement code runs for prediction and authority, so the sim cannot silently diverge.
