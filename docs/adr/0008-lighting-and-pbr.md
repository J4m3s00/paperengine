# 0008: Lighting and the PBR shader

Status: accepted

## Decision

- `core/shaders/pbr.vert` / `pbr.frag` are the engine's standard shader: glTF metallic-roughness with Cook-Torrance shading (GGX distribution, height-correlated Smith visibility, Schlick Fresnel, Lambert diffuse). Material parameters and texture slots use the glTF names (ADR 0007): `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `normalScale`, `occlusionStrength`, `alphaCutoff`; `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture`, `emissiveTexture`. A new material starts with every parameter zero, so callers set the ones they use (the importer sets all of them).
- Lights are entities with a `DirectionalLight {color, intensity}` component. A light shines along its entity's local -Z axis in world space (`lookRotation()` aims it); its position is ignored. There are no shadows.
- Ambient light is a constant `Scene::ambientLight` (no IBL). Its specular part is approximated with the normal-incidence reflectance, so fully metallic surfaces still show a faint color but are otherwise dark except for light highlights.
- `Renderer::render` gathers the lights each frame into the per-frame uniform block (set 0) as a list of `Light {vector, colorIntensity}` with room for 8; `vector.w == 0` marks a directional light, other values are reserved for point and spot lights. Extra lights are ignored with one warning.
- Shared GLSL lives in `core/shaders/include/` and is pulled in with `#include` (glslc resolves it relative to the including file); the build tracks these dependencies through glslc's depfile.
- Missing tangents (`tangent.w == 0`) are derived from screen-space derivatives of the UVs. When the UVs are degenerate (e.g. a mesh without texture coordinates) the normal map is skipped.
- No tone mapping: the swapchain is sRGB and values above 1 clip.

## Consequences

- Metallic surfaces look dark without environment lighting; image-based lighting would be the next step.
- Adding point/spot lights means a new component, new `Light` kinds in the shader loop and attenuation; the uniform layout already allows it.
