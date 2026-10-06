#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// Set 1: per material, filled from reflection (parameters are set by name).
layout(set = 1, binding = 0) uniform Material {
  vec4 baseColor;
  float normalMix;
  float alphaCutoff;
} material;

// Set by the Renderer from RenderState::alphaMode.
layout(constant_id = 0) const bool ALPHA_MASK = false;

void main() {
  if (ALPHA_MASK && material.baseColor.a < material.alphaCutoff) {
    discard;
  }
  // Placeholder shading until lighting exists: blend the base color with the
  // world normal.
  vec3 normalColor = normalize(vNormal) * 0.5 + 0.5;
  outColor = vec4(mix(material.baseColor.rgb, normalColor, material.normalMix), 1.0);
}
