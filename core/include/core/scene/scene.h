#pragma once

#include "core/assets/model.h"
#include "core/scene/components.h"

#include <entt/entity/registry.hpp>

namespace Paper {
// Thin wrapper around entt::registry. Owned by the caller; the Renderer only
// reads it.
class Scene {
public:
  // Creates an entity with Transform, Relationship and WorldTransform.
  entt::entity createEntity();

  // Attaches `child` under `parent` (entt::null detaches it into a root).
  // Keeps the local Transform; the new world transform appears after the next
  // TransformSystem::update.
  void setParent(entt::entity child, entt::entity parent);

  // Creates entities for every node of `model` (and one child entity with a
  // MeshRenderer per primitive) under a new root entity, which is returned. A
  // model can be instantiated any number of times. The Model's resources must
  // outlive the entities.
  entt::entity instantiate(const Model &model);

  // Constant ambient radiance added to every surface (there is no
  // image-based lighting).
  glm::vec3 ambientLight{0.03F};

  [[nodiscard]] entt::registry &registry() { return m_registry; }
  [[nodiscard]] const entt::registry &registry() const { return m_registry; }

private:
  entt::registry m_registry;
};
} // namespace Paper
