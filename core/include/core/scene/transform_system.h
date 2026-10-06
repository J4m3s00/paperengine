#pragma once

namespace Paper {
class Scene;

namespace TransformSystem {
// Recomputes every WorldTransform, parents before children. Call once per
// frame before Renderer::render.
void update(Scene &scene);
} // namespace TransformSystem
} // namespace Paper
