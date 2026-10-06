#version 450

#include "include/frame.glsl"

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vTangent;

layout(location = 0) out vec4 outColor;

// Set 1: per material. Parameters are set by name and textures by slot name;
// the names follow glTF so imported materials map directly (ADR 0007). A
// material starts with all parameters zero: set every one you use.
layout(set = 1, binding = 0) uniform Material {
  vec4 baseColorFactor;
  vec3 emissiveFactor;
  float metallicFactor;
  float roughnessFactor;
  float normalScale;
  float occlusionStrength;
  float alphaCutoff;
} material;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D normalTexture;
layout(set = 1, binding = 4) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;

// Set by the Renderer from RenderState::alphaMode.
layout(constant_id = 0) const bool ALPHA_MASK = false;

const float PI = 3.14159265359;

// Tangent frame from screen-space derivatives, used when the mesh has no
// tangents (Khronos glTF sample renderer). Returns false when the UVs are
// degenerate (e.g. a mesh without texture coordinates); the normal map is then
// ignored.
bool derivativeTangentFrame(vec3 normal, out mat3 tbn) {
  tbn = mat3(1.0);
  vec3 uvDx = dFdx(vec3(vUv, 0.0));
  vec3 uvDy = dFdy(vec3(vUv, 0.0));
  float det = uvDx.s * uvDy.t - uvDy.s * uvDx.t;
  if (abs(det) < 1e-20) {
    return false;
  }
  vec3 t = (uvDy.t * dFdx(vWorldPosition) - uvDx.t * dFdy(vWorldPosition)) / det;
  t -= normal * dot(normal, t);
  float lengthSquared = dot(t, t);
  if (!(lengthSquared > 1e-20)) { // Also rejects NaN.
    return false;
  }
  t *= inversesqrt(lengthSquared);
  tbn = mat3(t, cross(normal, t), normal);
  return true;
}

float distributionGgx(float nDotH, float alpha) {
  float a2 = alpha * alpha;
  float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

// Height-correlated Smith visibility (includes the 1 / (4 NdotL NdotV) term).
float visibilitySmithGgx(float nDotL, float nDotV, float alpha) {
  float a2 = alpha * alpha;
  float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
  float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
  return 0.5 / max(ggxV + ggxL, 1e-5);
}

vec3 fresnelSchlick(vec3 f0, float vDotH) {
  return f0 + (1.0 - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
}

void main() {
  vec4 baseColor = material.baseColorFactor * texture(baseColorTexture, vUv);
  if (ALPHA_MASK && baseColor.a < material.alphaCutoff) {
    discard;
  }

  vec4 metallicRoughness = texture(metallicRoughnessTexture, vUv);
  float metallic = clamp(material.metallicFactor * metallicRoughness.b, 0.0, 1.0);
  float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.04, 1.0);
  float alpha = roughness * roughness;
  float occlusion = 1.0 + material.occlusionStrength *
                              (texture(occlusionTexture, vUv).r - 1.0);
  vec3 emissive = texture(emissiveTexture, vUv).rgb * material.emissiveFactor;

  vec3 geometricNormal = normalize(vNormal);
  if (!gl_FrontFacing) {
    geometricNormal = -geometricNormal; // Double-sided materials.
  }
  mat3 tbn;
  bool hasTangentFrame = true;
  if (vTangent.w != 0.0) {
    vec3 t = normalize(vTangent.xyz - geometricNormal * dot(geometricNormal, vTangent.xyz));
    tbn = mat3(t, cross(geometricNormal, t) * vTangent.w, geometricNormal);
  } else {
    hasTangentFrame = derivativeTangentFrame(geometricNormal, tbn);
  }
  vec3 n = geometricNormal;
  if (hasTangentFrame) {
    vec3 tangentNormal = texture(normalTexture, vUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    n = normalize(tbn * tangentNormal);
  }

  vec3 v = normalize(frame.cameraPosition.xyz - vWorldPosition);
  float nDotV = max(dot(n, v), 1e-4);

  vec3 f0 = mix(vec3(0.04), baseColor.rgb, metallic);
  vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);

  // Constant ambient: no image-based lighting, so approximate its specular
  // part with the normal-incidence reflectance.
  vec3 color = frame.ambient.rgb * (diffuseColor + f0) * occlusion;

  for (int i = 0; i < frame.lightCount.x; ++i) {
    Light light = frame.lights[i];
    if (light.vector.w != 0.0) {
      continue; // Only directional lights so far.
    }
    vec3 l = normalize(-light.vector.xyz);
    float nDotL = dot(n, l);
    if (nDotL <= 0.0) {
      continue;
    }
    vec3 h = normalize(l + v);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);

    vec3 fresnel = fresnelSchlick(f0, vDotH);
    vec3 specular = fresnel * distributionGgx(nDotH, alpha) *
                    visibilitySmithGgx(nDotL, nDotV, alpha);
    vec3 diffuse = (1.0 - fresnel) * diffuseColor / PI;
    vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;
    color += (diffuse + specular) * radiance * nDotL;
  }

  outColor = vec4(color + emissive, 1.0);
}
