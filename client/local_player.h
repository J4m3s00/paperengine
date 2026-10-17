#pragma once

#include "base/cvar.h"
#include "base/fixed_timestep.h"
#include "core/renderer/camera.h"
#include "core/window.h"
#include "game/collision.h"
#include "game/movement.h"
#include "game/replay.h"
#include "game/tick.h"
#include "net/client_session.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// The locally controlled player: turns window input into one UserCmd per
// simulation tick (128 Hz), runs the shared movement code, and produces a
// camera interpolated between the last two ticks. View angles update every
// frame (not per tick) so mouse look has no tick-rate latency.
class LocalPlayer {
public:
  LocalPlayer(const Paper::CollisionWorld &world, const Paper::MovementCvars &movement,
              Paper::CvarRegistry &registry = Paper::cvars())
      : m_world(world), m_movement(movement), m_step(Paper::kServerTickSeconds),
        m_sensitivity(registry.add<float>("sensitivity", 2.0F, 0.01F, 50.0F,
                                          "Mouse sensitivity (CS: degrees = 0.022 * value)")) {}

  // Online play: the session predicts and reconciles instead of the local
  // movement code (it runs the same `movePlayer`). Null = offline.
  void setSession(Paper::ClientSession *session) { m_session = session; }

  void respawn(const glm::vec3 &origin, float yaw) {
    m_state = {};
    m_state.origin = origin;
    m_prev = m_state;
    m_yaw = yaw;
    m_pitch = 0.0F;
    m_eyeHeight = Paper::kStandingEyeHeight;
    restartRecording();
  }

  // Records every tick's usercmd from the next respawn() on; the recording
  // restarts at each respawn so it always begins at a known state.
  void enableRecording(std::string mapName) {
    m_recording.emplace();
    m_recording->map = std::move(mapName);
    restartRecording();
  }
  // The recording so far, with the current state as its expected end state.
  [[nodiscard]] std::optional<Paper::Replay> recording() const {
    if (!m_recording) {
      return std::nullopt;
    }
    Paper::Replay replay = *m_recording;
    replay.expected = m_state;
    return replay;
  }

  // `lookEnabled`: apply mouse look and movement keys (false while a UI has the
  // mouse, though the simulation keeps running).
  void update(const Paper::Window &window, double frameSeconds, bool inputEnabled) {
    if (inputEnabled) {
      const glm::dvec2 delta = window.mouseDelta();
      const float radiansPerCount = glm::radians(0.022F) * m_sensitivity.value();
      m_yaw -= static_cast<float>(delta.x) * radiansPerCount;
      m_pitch = std::clamp(m_pitch - static_cast<float>(delta.y) * radiansPerCount,
                           -kMaxPitch, kMaxPitch);
    }

    // Keys are sampled once per frame and held for every tick of that frame.
    Paper::UserCmd cmd;
    cmd.yaw = m_yaw;
    cmd.pitch = m_pitch;
    if (inputEnabled) {
      cmd.forward = (window.keyDown(Paper::Key::W) ? 1.0F : 0.0F) -
                    (window.keyDown(Paper::Key::S) ? 1.0F : 0.0F);
      cmd.side = (window.keyDown(Paper::Key::D) ? 1.0F : 0.0F) -
                 (window.keyDown(Paper::Key::A) ? 1.0F : 0.0F);
      cmd.jump = window.keyDown(Paper::Key::Space);
      cmd.duck = window.keyDown(Paper::Key::LeftControl);
      cmd.walk = window.keyDown(Paper::Key::LeftShift);
    }

    m_step.advance(m_session ? frameSeconds * m_session->timeScale() : frameSeconds);
    const Paper::MovementParams params = m_movement.params();
    if (m_recording && m_recording->cmds.empty()) {
      m_recording->params = params;
    }
    while (m_step.consumeStep()) {
      m_prev = m_state;
      if (m_session) {
        m_session->tick(cmd);
        if (m_session->playing()) {
          m_state = m_session->predicted();
          // Draw with the fading correction applied, not the raw prediction.
          m_state.origin = m_session->visualOrigin();
          if (!m_havePrev) {
            m_prev = m_state;
            m_havePrev = true;
          }
        }
        continue;
      }
      if (m_recording) {
        m_recording->cmds.push_back(cmd);
      }
      Paper::movePlayer(m_state, cmd, m_world, params, Paper::kDefaultMaxSpeed,
                        static_cast<float>(m_step.stepSeconds()));
    }

    // Eye height eases between standing and crouched instead of snapping.
    const float targetEye = m_state.ducked ? Paper::kDuckedEyeHeight : Paper::kStandingEyeHeight;
    const float maxChange = kEyeSpeed * static_cast<float>(frameSeconds);
    m_eyeHeight += std::clamp(targetEye - m_eyeHeight, -maxChange, maxChange);
  }

  [[nodiscard]] const Paper::PlayerState &state() const { return m_state; }
  [[nodiscard]] float yaw() const { return m_yaw; }
  [[nodiscard]] float pitch() const { return m_pitch; }

  // The view for rendering: position blended between the last two ticks,
  // orientation straight from the latest mouse input.
  [[nodiscard]] Paper::Camera camera(Paper::Camera base) const {
    const auto alpha = static_cast<float>(m_step.alpha());
    base.position = glm::mix(m_prev.origin, m_state.origin, alpha) +
                    glm::vec3(0.0F, m_eyeHeight, 0.0F);
    base.rotation = glm::angleAxis(m_yaw, glm::vec3(0.0F, 1.0F, 0.0F)) *
                    glm::angleAxis(m_pitch, glm::vec3(1.0F, 0.0F, 0.0F));
    return base;
  }

private:
  void restartRecording() {
    if (m_recording) {
      m_recording->cmds.clear();
      m_recording->start = m_state;
      m_recording->params = m_movement.params();
      m_recording->maxSpeed = Paper::kDefaultMaxSpeed;
    }
  }

  static constexpr float kMaxPitch = glm::radians(89.0F);
  static constexpr float kEyeSpeed = 200.0F; // units per second

  const Paper::CollisionWorld &m_world;
  const Paper::MovementCvars &m_movement;
  Paper::FixedTimestep m_step;
  Paper::Cvar<float> &m_sensitivity;
  Paper::PlayerState m_state;
  Paper::PlayerState m_prev;
  float m_yaw = 0.0F;
  float m_pitch = 0.0F;
  float m_eyeHeight = Paper::kStandingEyeHeight;
  std::optional<Paper::Replay> m_recording;
  Paper::ClientSession *m_session = nullptr;
  bool m_havePrev = false;
};
