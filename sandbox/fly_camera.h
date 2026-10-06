#pragma once

#include "core/renderer/camera.h"
#include "core/window.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

// Free-fly camera controller. Hold the right mouse button to look around
// (the cursor is captured while held), WASD to move, E/Space up, Q/Ctrl down,
// Shift to go faster, mouse wheel to change the base speed.
class FlyCamera {
public:
  FlyCamera(const glm::vec3 &position, const glm::vec3 &target) {
    m_camera.position = position;
    const glm::vec3 forward = glm::normalize(target - position);
    m_yaw = std::atan2(-forward.x, -forward.z);
    m_pitch = std::asin(forward.y);
    applyRotation();
  }

  void update(Paper::Window &window, float dt) {
    const bool looking = window.mouseDown(Paper::MouseButton::Right);
    if (looking != m_looking) {
      m_looking = looking;
      window.setCursorMode(looking ? Paper::CursorMode::Captured
                                   : Paper::CursorMode::Normal);
    }
    if (m_looking) {
      const glm::dvec2 delta = window.mouseDelta();
      m_yaw -= static_cast<float>(delta.x) * kLookSensitivity;
      m_pitch -= static_cast<float>(delta.y) * kLookSensitivity;
      m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);
      applyRotation();
    }

    m_speed = std::clamp(
        m_speed * std::pow(1.1F, static_cast<float>(window.scrollDelta().y)),
        0.1F, 100.0F);

    const glm::vec3 forward = m_camera.rotation * glm::vec3(0.0F, 0.0F, -1.0F);
    const glm::vec3 right = m_camera.rotation * glm::vec3(1.0F, 0.0F, 0.0F);
    const glm::vec3 up(0.0F, 1.0F, 0.0F);

    glm::vec3 move(0.0F);
    if (window.keyDown(Paper::Key::W)) {
      move += forward;
    }
    if (window.keyDown(Paper::Key::S)) {
      move -= forward;
    }
    if (window.keyDown(Paper::Key::D)) {
      move += right;
    }
    if (window.keyDown(Paper::Key::A)) {
      move -= right;
    }
    if (window.keyDown(Paper::Key::E) || window.keyDown(Paper::Key::Space)) {
      move += up;
    }
    if (window.keyDown(Paper::Key::Q) ||
        window.keyDown(Paper::Key::LeftControl)) {
      move -= up;
    }
    if (glm::dot(move, move) > 0.0F) {
      const float boost = window.keyDown(Paper::Key::LeftShift) ? 3.0F : 1.0F;
      m_camera.position += glm::normalize(move) * m_speed * boost * dt;
    }
  }

  [[nodiscard]] const Paper::Camera &camera() const { return m_camera; }

private:
  static constexpr float kLookSensitivity = 0.0025F;
  static constexpr float kMaxPitch = glm::radians(89.0F);

  void applyRotation() {
    m_camera.rotation =
        glm::angleAxis(m_yaw, glm::vec3(0.0F, 1.0F, 0.0F)) *
        glm::angleAxis(m_pitch, glm::vec3(1.0F, 0.0F, 0.0F));
  }

  Paper::Camera m_camera;
  float m_yaw = 0.0F;
  float m_pitch = 0.0F;
  float m_speed = 3.0F;
  bool m_looking = false;
};
