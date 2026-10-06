#include "core/scene/transform_system.h"

#include "core/scene/scene.h"

namespace Paper::TransformSystem {
namespace {

void updateSubtree(entt::registry &registry, entt::entity entity,
                   const glm::mat4 &parentWorld) {
  const glm::mat4 world =
      parentWorld * registry.get<Transform>(entity).matrix();
  registry.get<WorldTransform>(entity).matrix = world;
  for (entt::entity child = registry.get<Relationship>(entity).firstChild;
       child != entt::null;
       child = registry.get<Relationship>(child).nextSibling) {
    updateSubtree(registry, child, world);
  }
}
} // namespace

void update(Scene &scene) {
  entt::registry &registry = scene.registry();
  for (const entt::entity entity : registry.view<Relationship>()) {
    if (registry.get<Relationship>(entity).parent == entt::null) {
      updateSubtree(registry, entity, glm::mat4(1.0F));
    }
  }
}
} // namespace Paper::TransformSystem
