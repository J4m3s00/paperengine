#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Paper {
// Standalone camera passed to Renderer::render. Right-handed, Y up, looking
// down its local -Z axis. The projection (depth 0..1) is built by the Renderer
// from the swapchain aspect ratio.
struct Camera {
  glm::vec3 position{0.0F, 0.0F, 3.0F};
  glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  float fovY = glm::radians(60.0F);
  float nearPlane = 0.1F;
  float farPlane = 100.0F;

  [[nodiscard]] glm::mat4 view() const;
  [[nodiscard]] static Camera lookAt(const glm::vec3 &eye,
                                     const glm::vec3 &target,
                                     const glm::vec3 &up = {0.0F, 1.0F, 0.0F});
};
} // namespace Paper
