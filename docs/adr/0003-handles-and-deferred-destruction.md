# 0003: Handles, deferred destruction, per-frame material UBOs

Status: accepted

## Decision

- The Renderer owns every resource; callers hold `Handle<Tag>` (index + generation). A stale handle is detected and asserts instead of crashing.
- Freeing a resource puts it in a deletion queue. It is destroyed only after the frames in flight that may still use it have finished (2 frames in flight).
- Each Material keeps one UBO copy per frame in flight. `material.set` marks it dirty and the copies are updated when drawing, so a parameter change never races with a frame the GPU is still reading.

## Consequences

- Meshes and Materials can be changed or freed at runtime without validation errors or flickering.
- Costs a small amount of memory per Material and a generation check per handle lookup.
