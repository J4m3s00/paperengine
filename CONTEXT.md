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
- **Render state**: Part of a Material. The Renderer caches pipelines by (shader, render state, vertex layout, attachment formats).
- **Model**: Result of `loadModel(path)` for a glTF file: meshes, materials, textures and a node tree. `scene.instantiate(model)` creates the entities with their hierarchy and returns the root entity.
- **Frame in flight**: One of 2 CPU-side frames prepared while the GPU renders the previous one. Per-frame resources (UBO copies, command buffers, sync objects) exist once per frame in flight.
- **Deferred destruction**: Freed resources wait in a queue until the frames in flight that may use them have finished.
- **Binding conventions**: Set 0 = per frame (camera, light), set 1 = per material, per-object model matrix via push constants. Fixed vertex layout: location 0 position, 1 normal, 2 uv, 3 tangent.
- **Error**: `Paper::Error {ErrorCode code; std::string message;}`, returned as `std::expected<T, Error>`.
- **Conventions**: Right-handed, Y up, depth 0..1, negative viewport height for the Vulkan Y flip, counter-clockwise front face, sRGB swapchain with linear shading.
