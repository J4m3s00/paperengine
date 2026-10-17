# 0017: GPU skinning with a joint palette; animation is a headless library

Status: accepted

## Decision

- Skinning runs on the GPU. Every vertex carries 4 joint indices and 4 unorm8 weights (attributes 4 and 5 of the fixed layout, 8 bytes). All skinned instances of a frame write their skinning matrices into one host-visible storage buffer (per frame in flight, set 0 binding 1); the skinned vertex shader finds an instance's matrices through the draw's `firstInstance` (`gl_InstanceIndex`), so no extra push constants or descriptor sets are needed and skinned draws sort and batch like any other.
- Animation (skeleton, clips, sampling, blending, the animator) lives in the headless `anim` library with unit tests; the importer fills it, the game drives it, the renderer only sees matrices.
- Hitboxes stay fixed (ADR 0011). The model is a picture of the player; it is not what the server tests shots against.

## Why

- A CPU-skinned mesh would need a vertex upload per player per frame; the palette costs one memcpy of ~4 KB per character.
- `firstInstance` indexing avoids growing the 128 byte push constant block and keeps one pipeline layout for all shaders.
- Headless animation makes the logic (cross-fades, masks, looping) testable without a GPU and reusable on the server if skeleton hitboxes come later.

## Consequences

- Static meshes pay 8 bytes per vertex for the unused joint attributes (pipelines only enable them for shaders that read them).
- At most 8192 joint matrices per frame (about 125 characters of 65 joints).
- A skeleton-driven hitbox provider must sample animations on the server; measure that before switching.
