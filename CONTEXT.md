# Paper Engine: Domain Glossary

Vocabulary for the rendering pipeline. Use these terms in issues, code and tests.

- **Renderer**: Public entry point for drawing. Owns `VulkanContext`, `Swapchain` and every GPU resource. Created via `Renderer::create(window)`. `sandbox` never sees `Vk*` types.
- **Scene**: Thin wrapper around `entt::registry`. Owned by the caller; the Renderer only reads it in `render(scene, camera)`.
- **Entity / Component**: ECS terms from `entt`. Components: `Transform` (local TRS), `Relationship` (parent, first child, next sibling), `WorldTransform` (computed), `MeshRenderer {MeshHandle, MaterialHandle}`, `DirectionalLight`.
- **TransformSystem**: Computes every `WorldTransform` (parents before children). The caller runs `TransformSystem::update(scene)` before `render`; the Renderer never mutates the Scene.
- **Camera**: Standalone object passed to `render`. Not part of the Scene.
- **Handle**: `Handle<Tag>` (index + generation). Refers to a resource owned by the Renderer (`MeshHandle`, `MaterialHandle`, `ShaderHandle`, `TextureHandle`, `ModelHandle`). A stale handle is a programmer error.
- **Mesh**: One vertex buffer + one index buffer = one draw. A glTF mesh with N primitives becomes N Meshes at import.
- **Shader**: Compiled SPIR-V plus its reflection data. GLSL sources live in `core/shaders/` and are compiled by `glslc` at build time.
- **Material**: Shader + parameter values + textures + render state (cull mode, alpha mode, depth test). Parameters are set by name and validated against the Shader's reflection.
- **Texture**: Sampled 2D image with a full mip chain, created from an `Image` (RGBA8) with a `ColorSpace` and `SamplerSettings`. `TextureHandle`. See ADR 0006.
- **Texture slot**: A `sampler2D` variable in a shader's set 1 (binding >= 1), assigned by name with `setMaterialTexture`. Unassigned slots use a **default texture** (white, or flat normal for slots named `*normal*`).
- **Light**: An entity with a `DirectionalLight {color, intensity}`; it shines along the entity's local -Z. The Scene also has a constant `ambientLight`. See ADR 0008.
- **Render state**: Part of a Material. The Renderer caches pipelines by (shader, render state, vertex layout, attachment formats).
- **Model**: Result of `Renderer::loadModel(path, shader)` for a glTF/GLB file: a plain-data node tree (`Model`/`ModelNode`) plus the meshes, materials and textures it owns (`ModelHandle`). `scene.instantiate(renderer.model(handle))` creates the entities with their hierarchy and returns the root entity. See ADR 0007.
- **Frame in flight**: One of 2 CPU-side frames prepared while the GPU renders the previous one. Per-frame resources (UBO copies, command buffers, sync objects) exist once per frame in flight.
- **Deferred destruction**: Freed resources wait in a queue until the frames in flight that may use them have finished.
- **Binding conventions**: Set 0 = per frame (camera, light), set 1 = per material, per-object model matrix via push constants. Fixed vertex layout: location 0 position, 1 normal, 2 uv, 3 tangent.
- **Error**: `Paper::Error {ErrorCode code; std::string message;}`, returned as `std::expected<T, Error>`.
- **Conventions**: Right-handed, Y up, depth 0..1, negative viewport height for the Vulkan Y flip, counter-clockwise front face, sRGB swapchain with linear shading.

## Game (planned, see `docs/plan/`)

**Simulation**:
The shared, headless gameplay code (movement, collision, weapons, rules) that runs identically on server and client.
_Avoid_: game logic, physics

**Server**:
The single authoritative process that runs the Simulation and decides positions, hits and damage.
_Avoid_: host

**Tick**:
One fixed step of the Simulation (128 per second on the Server).
_Avoid_: frame (a frame is a rendered image)

**Usercmd**:
One timestamped bundle of a player's input (buttons, view angles, movement) sent from client to Server.
_Avoid_: input packet

**Snapshot**:
The Server's state of the replicated entities at one Tick, sent to clients (delta-compressed against an acked baseline).
_Avoid_: update, state packet

**Prediction**:
The client running the Simulation for its own player ahead of the Server, using Usercmds the Server has not acknowledged yet.

**Reconciliation**:
Correcting the client's predicted state to the Server's state and replaying unacknowledged Usercmds.

**Interpolation**:
Rendering remote players slightly in the past, blended between two Snapshots.

**Lag compensation**:
The Server rewinding other players' hitboxes to the tick the shooter saw before resolving a shot.

**Hitbox**:
A volume on a player that a shot can hit, each with a damage multiplier (head, chest, ...). Supplied by a `HitboxProvider`.

**Hull trace**:
A swept test of a player-sized axis-aligned box against the world, used by movement.
_Avoid_: raycast (that is a zero-width trace)

**Spray pattern**:
The fixed per-weapon sequence of recoil offsets while firing continuously.

**Dev world**:
The first playable: one explorable map with stationary bots for practising movement and shooting.

**Cvar**:
A named, typed, runtime-tunable setting (e.g. air acceleration).
_Avoid_: config variable, setting
