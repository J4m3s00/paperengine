// Set 0: per frame, filled by the Renderer. Must match FrameUniforms in
// core/src/renderer/renderer.cpp (std140).
const int MAX_LIGHTS = 8;

struct Light {
  // xyz: direction the light travels in (world space), w == 0 for a
  // directional light. Other w values are reserved for point/spot lights.
  vec4 vector;
  // rgb: color, a: intensity.
  vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform FrameData {
  mat4 view;
  mat4 proj;
  mat4 viewProj;
  vec4 cameraPosition;
  vec4 ambient;     // rgb: constant ambient radiance.
  ivec4 lightCount; // x: number of valid entries in `lights`.
  Light lights[MAX_LIGHTS];
} frame;
