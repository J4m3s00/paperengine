#version 450

#include "include/frame.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

// Joint matrices of every skinned object drawn this frame; an object's first
// matrix is its draw's firstInstance (gl_InstanceIndex).
layout(std430, set = 0, binding = 1) readonly buffer JointPalette {
  mat4 joints[];
} palette;

// Per object.
layout(push_constant) uniform Push {
  mat4 model;
  mat4 normalMatrix;
} push;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vTangent;

void main() {
  uint base = uint(gl_InstanceIndex);
  mat4 skin = palette.joints[base + inJoints.x] * inWeights.x +
              palette.joints[base + inJoints.y] * inWeights.y +
              palette.joints[base + inJoints.z] * inWeights.z +
              palette.joints[base + inJoints.w] * inWeights.w;
  vec4 skinned = skin * vec4(inPosition, 1.0);
  vec4 worldPosition = push.model * skinned;
  gl_Position = frame.viewProj * worldPosition;
  vWorldPosition = worldPosition.xyz;
  vNormal = mat3(push.normalMatrix) * (mat3(skin) * inNormal);
  vUv = inUv;
  vTangent = vec4(mat3(push.model) * (mat3(skin) * inTangent.xyz), inTangent.w);
}
