# 0004: Fixed binding conventions and vertex layout

Status: accepted

## Decision

- Set 0 is per frame (camera matrices, light, frame data), filled by the Renderer.
- Set 1 is per material (parameter UBO and textures), filled from SPIR-V reflection.
- The per-object model matrix is passed as push constants (128 bytes: `mat4 model` plus the normal matrix).
- The vertex layout is fixed: location 0 position, 1 normal, 2 uv, 3 tangent. Custom shaders must follow it.
- No bindless / descriptor indexing.

## Consequences

- Shaders and pipelines stay simple; the Material only reflects set 1.
- Bindless is limited on MoltenVK and unnecessary at this scale; it can be revisited later.
- A free vertex layout from reflection is not supported.
