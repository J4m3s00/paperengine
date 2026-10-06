#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
  // Placeholder shading until materials exist: visualise the world normal.
  outColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
