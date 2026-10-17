#pragma once

#include "core/renderer/material.h"
#include "core/renderer/mesh.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Paper {
// Local transform relative to the parent (or the world for roots).
struct Transform {
  glm::vec3 position{0.0F};
  glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  glm::vec3 scale{1.0F};

  [[nodiscard]] glm::mat4 matrix() const {
    return glm::translate(glm::mat4(1.0F), position) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0F), scale);
  }
};

// Intrusive tree links; edit them only through Scene::setParent.
struct Relationship {
  entt::entity parent = entt::null;
  entt::entity firstChild = entt::null;
  entt::entity prevSibling = entt::null;
  entt::entity nextSibling = entt::null;
};

// Computed by TransformSystem::update; read-only for everyone else.
struct WorldTransform {
  glm::mat4 matrix{1.0F};
};

// Directional light. It shines along the entity's local -Z axis (like a
// camera looks), so rotate the entity's Transform to aim it; its position is
// irrelevant. See lookRotation().
struct DirectionalLight {
  glm::vec3 color{1.0F};
  float intensity = 3.0F;
};

// Rotation that makes the local -Z axis point along `direction`.
[[nodiscard]] inline glm::quat
lookRotation(const glm::vec3 &direction,
             const glm::vec3 &up = {0.0F, 1.0F, 0.0F}) {
  return glm::quat_cast(
      glm::inverse(glm::lookAt(glm::vec3(0.0F), direction, up)));
}

// Joint matrices of a skinned mesh, in the mesh's model space: for each joint
// `jointWorld * inverseBind`. Set it every frame (see the `anim` library); the
// entity's MeshRenderer must use a skinning shader (pbr_skinned.vert).
struct SkinPose {
  std::vector<glm::mat4> joints;
};

struct MeshRenderer {
  MeshHandle mesh;
  MaterialHandle material;
};
} // namespace Paper
