# 0002: `entt` Scene with an explicit transform update

Status: accepted

## Decision

`Scene` is a thin wrapper around `entt::registry`; `entt` is a PUBLIC dependency of `core`. The hierarchy is a `Relationship` component (parent, first child, next sibling). `TransformSystem::update(scene)` recomputes every `WorldTransform` each frame, parents before children, without dirty flags.

The caller runs the update before `Renderer::render(scene, camera)`. `render` only reads the Scene and has no side effects on it. The Scene is owned by the caller. The Camera is a separate object, not part of the Scene.

## Consequences

- glTF node trees map to entities without loss.
- Dirty flags can be added later if many static objects make the full recompute costly.
- Multiple cameras or viewports need no change to the Scene.
