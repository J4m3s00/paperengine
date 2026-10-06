#pragma once

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

  [[nodiscard]] entt::registry &registry() { return m_registry; }
  [[nodiscard]] const entt::registry &registry() const { return m_registry; }

private:
  entt::registry m_registry;
};
} // namespace Paper
