# M0: Foundations (size M)

Prepare the repo so the later milestones can be built, tested and measured.

## Scope

- Split targets per ADR 0012: `game`, `net`, `client`, `server` (headless stub), moving code out of `core` where the server needs it (math, error type, logging must not pull in Vulkan or GLFW). `sandbox` stays.
- Fixed-timestep main loop helper (accumulator, interpolation alpha) usable by client and server.
- Test setup: doctest via FetchContent, CTest, a first test in `game`.
- Tracy profiler integration, off in release unless enabled; frame/tick zones in the renderer and loop.
- Dear ImGui (Vulkan + GLFW backends) in the client for a debug overlay: frame time graph, cvar console.
- Cvar system: named, typed, runtime-tunable values (needed for movement tuning in M1).
- Build presets for Windows/Linux/macOS compile checks; CI is optional.

## Exit gate

- `server` builds and runs without Vulkan/GLFW.
- `ctest` runs and passes.
- Debug overlay shows frame time and cvars in the client.
- CLAUDE.md and CONTEXT.md describe the new layout.
