#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

// Set 0: per frame, filled by the Renderer.
layout(set = 0, binding = 0) uniform FrameData {
  mat4 view;
  mat4 proj;
  mat4 viewProj;
  vec4 cameraPosition;
} frame;

// Per object.
layout(push_constant) uniform Push {
  mat4 model;
  mat4 normalMatrix;
} push;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;

void main() {
  gl_Position = frame.viewProj * push.model * vec4(inPosition, 1.0);
  vNormal = mat3(push.normalMatrix) * inNormal;
  vUv = inUv;
}
