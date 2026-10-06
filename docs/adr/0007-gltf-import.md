# 0007: glTF import

Status: accepted

## Decision

- `fastgltf` (v0.9.1, fetched with its simdjson) parses `.gltf` and `.glb`. The importer (`core/src/assets/gltf_loader.cpp`) only uses the Renderer's public resource API; `Renderer::loadModel(path, shader)` wraps it and stores the result in a pool.
- A `Model` (`core/assets/model.h`) is plain data: a node tree (`ModelNode`: TRS transform, children, one `MeshRenderer` per glTF primitive), the default scene's root nodes, model-space bounds, and the handles of the meshes, materials and textures it created. `Renderer::model(handle)` returns it; `Renderer::destroyModel` frees those resources (the shader stays with the caller).
- `Scene::instantiate(const Model&)` creates a new root entity, one entity per node (with `Relationship` hierarchy and local `Transform`) and one child entity with a `MeshRenderer` per primitive. A model can be instantiated any number of times.
- A glTF mesh with N primitives becomes N engine meshes, shared by every node that references the mesh. Only triangle lists are supported; other primitive modes are skipped with a warning.
- Missing normals are generated (smooth); missing UVs are zero; missing tangents have `tangent.w == 0`, which tells shaders to derive a tangent frame from the UVs.
- All imported materials use the shader given to `loadModel`. glTF properties are mapped by name onto the parameters and texture slots the shader declares, and names the shader lacks are skipped:
  - parameters: `baseColorFactor` (vec4), `metallicFactor`, `roughnessFactor`, `emissiveFactor` (vec3, multiplied by `emissiveStrength`), `normalScale`, `occlusionStrength`, `alphaCutoff`;
  - texture slots: `baseColorTexture` and `emissiveTexture` (sRGB), `metallicRoughnessTexture`, `normalTexture`, `occlusionTexture` (linear). A glTF texture used with two color spaces is uploaded twice.
- Render state: `doubleSided` selects `CullMode::None`, `MASK` selects `AlphaMode::Mask`; `BLEND` is rendered opaque with a warning. Only `TEXCOORD_0` is used. Extensions such as `KHR_texture_transform` are not applied.

## Consequences

- Shader authors who want glTF materials use the names above (the PBR shader of the lighting milestone will declare all of them).
- Importing is synchronous and uploads each buffer and texture separately, which is slow for very large models.
- Embedded and external buffers/images (including data URIs) work; KTX2/WebP/DDS textures do not.
