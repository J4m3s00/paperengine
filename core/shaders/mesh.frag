#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// Set 1: per material, filled from reflection. Parameters are set by name,
// textures by slot name (sampler2D variable name).
layout(set = 1, binding = 0) uniform Material {
  vec4 baseColor;
  float normalMix;
  float alphaCutoff;
} material;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

// Set by the Renderer from RenderState::alphaMode.
layout(constant_id = 0) const bool ALPHA_MASK = false;

void main() {
  vec4 albedo = material.baseColor * texture(baseColorTexture, vUv);
  if (ALPHA_MASK && albedo.a < material.alphaCutoff) {
    discard;
  }
  // Placeholder shading until lighting exists: blend the albedo with the world
  // normal.
  vec3 normalColor = normalize(vNormal) * 0.5 + 0.5;
  outColor = vec4(mix(albedo.rgb, normalColor, material.normalMix), 1.0);
}
