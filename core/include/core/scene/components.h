#pragma once

#include "core/renderer/mesh.h"

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

struct MeshRenderer {
  MeshHandle mesh;
};
} // namespace Paper
