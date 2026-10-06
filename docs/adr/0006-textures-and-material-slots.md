# 0006: Textures and material texture slots

Status: accepted

## Decision

- `stb_image` decodes images (`loadImage`/`decodeImage` in `core/assets/image.h`, always RGBA8). `Renderer::createTexture` uploads an `Image` synchronously and generates a full mip chain with a blit chain; the image is left in `SHADER_READ_ONLY_OPTIMAL`.
- The creator picks the color space: `ColorSpace::Srgb` for base color and emissive, `ColorSpace::Linear` for normal, metallic-roughness and occlusion. The engine cannot infer it from the slot.
- Samplers are cached by `SamplerSettings` (filters, wrap modes). Mipmaps and anisotropic filtering (4x, or the device maximum if lower) are always on; `samplerAnisotropy` is a required device feature.
- Texture slots are `sampler2D` variables in set 1 at binding >= 1 (binding 0 stays the parameter block, see ADR 0004). Slots are assigned by variable name with `setMaterialTexture`; names are validated against the reflection.
- Unassigned slots use an engine-owned default texture: a flat normal (0.5, 0.5, 1.0) for slots whose name contains "normal" (case-insensitive), white otherwise. `loadTextureOrDefault` logs a warning and returns a default when a file cannot be loaded.
- Texture descriptors follow the same dirty mechanism as the parameter block (ADR 0003): `setMaterialTexture` marks every frame's descriptor set stale, and a set is rewritten when its frame is drawn.
- A texture cannot be destroyed while a material slot references it (use count), and default textures cannot be destroyed. Destruction goes through the deletion queue.

## Consequences

- Shaders have at most 8 texture slots.
- The naming convention for the flat-normal default is implicit; renaming a normal-map slot to something without "normal" falls back to white until the caller assigns a texture.
