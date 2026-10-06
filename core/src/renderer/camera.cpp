#include "core/renderer/camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Paper {

glm::mat4 Camera::view() const {
  const glm::mat4 world =
      glm::translate(glm::mat4(1.0F), position) * glm::mat4_cast(rotation);
  return glm::inverse(world);
}

Camera Camera::lookAt(const glm::vec3 &eye, const glm::vec3 &target,
                      const glm::vec3 &up) {
  Camera camera;
  camera.position = eye;
  // glm::lookAt builds the view matrix; its inverse rotation is the camera's.
  camera.rotation = glm::quat_cast(glm::inverse(glm::lookAt(eye, target, up)));
  return camera;
}
} // namespace Paper
