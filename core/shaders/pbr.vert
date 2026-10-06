#version 450

#include "include/frame.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

// Per object.
layout(push_constant) uniform Push {
  mat4 model;
  mat4 normalMatrix;
} push;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
// xyz: world-space tangent, w: handedness (0 when the mesh has no tangents).
layout(location = 3) out vec4 vTangent;

void main() {
  vec4 worldPosition = push.model * vec4(inPosition, 1.0);
  gl_Position = frame.viewProj * worldPosition;
  vWorldPosition = worldPosition.xyz;
  vNormal = mat3(push.normalMatrix) * inNormal;
  vUv = inUv;
  vTangent = vec4(mat3(push.model) * inTangent.xyz, inTangent.w);
}
