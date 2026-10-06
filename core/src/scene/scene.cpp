#include "core/scene/scene.h"

#include <cassert>

namespace Paper {

entt::entity Scene::createEntity() {
  const entt::entity entity = m_registry.create();
  m_registry.emplace<Transform>(entity);
  m_registry.emplace<Relationship>(entity);
  m_registry.emplace<WorldTransform>(entity);
  return entity;
}

void Scene::setParent(entt::entity child, entt::entity parent) {
  assert(child != parent);
#ifndef NDEBUG
  for (entt::entity e = parent; e != entt::null;
       e = m_registry.get<Relationship>(e).parent) {
    assert(e != child && "setParent would create a cycle");
  }
#endif

  Relationship &rel = m_registry.get<Relationship>(child);
  if (rel.parent != entt::null) {
    Relationship &oldParent = m_registry.get<Relationship>(rel.parent);
    if (oldParent.firstChild == child) {
      oldParent.firstChild = rel.nextSibling;
    }
    if (rel.prevSibling != entt::null) {
      m_registry.get<Relationship>(rel.prevSibling).nextSibling =
          rel.nextSibling;
    }
    if (rel.nextSibling != entt::null) {
      m_registry.get<Relationship>(rel.nextSibling).prevSibling =
          rel.prevSibling;
    }
  }

  rel.parent = parent;
  rel.prevSibling = entt::null;
  rel.nextSibling = entt::null;
  if (parent != entt::null) {
    Relationship &newParent = m_registry.get<Relationship>(parent);
    rel.nextSibling = newParent.firstChild;
    if (newParent.firstChild != entt::null) {
      m_registry.get<Relationship>(newParent.firstChild).prevSibling = child;
    }
    newParent.firstChild = child;
  }
}
} // namespace Paper
