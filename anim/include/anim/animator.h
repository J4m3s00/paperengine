#pragma once

#include "anim/pose.h"

#include <vector>

namespace Paper {
// Plays one clip at a time with cross-fades, plus one optional overlay clip
// on part of the body (shooting or reloading while running).
class Animator {
public:
  explicit Animator(const AnimationLibrary &library);

  // Starts `clip` (an index into the library). A running clip is faded out
  // over `fadeSeconds`. Playing the clip that already plays does nothing
  // unless `restart`.
  void play(int clip, float fadeSeconds = 0.2F, bool restart = false);
  // Same clip for a locomotion blend: keeps the phase of the clip that
  // plays now (walk -> run without a foot pop). Falls back to play().
  void playKeepingPhase(int clip, float fadeSeconds = 0.2F);

  // Plays `clip` over the joints in `mask` (see maskBelow), fading in and, for
  // a one-shot, out again at its end. With `loop` it repeats until replaced
  // or stopped (`clip` < 0).
  void playOverlay(int clip, std::vector<float> mask, float fadeSeconds = 0.1F, bool loop = false);
  // The overlay clip that plays now, or -1.
  [[nodiscard]] int overlayClip() const { return m_overlay; }
  void setSpeed(float speed) { m_speed = speed; }

  void update(float dt);

  [[nodiscard]] const Pose &pose() const { return m_pose; }
  [[nodiscard]] int current() const { return m_current; }
  [[nodiscard]] float time() const { return m_time; }
  [[nodiscard]] bool overlayActive() const { return m_overlay >= 0; }
  // True once a non-looping current clip has reached its end.
  [[nodiscard]] bool finished() const;

  // Skinning matrices of the current pose (and optionally model-space joint
  // matrices, for attaching things like a weapon to a hand).
  void computeMatrices(std::vector<glm::mat4> &skin, std::vector<glm::mat4> *world = nullptr) const;

private:
  const AnimationLibrary *m_library;
  int m_current = -1;
  float m_time = 0.0F;
  int m_previous = -1;
  float m_previousTime = 0.0F;
  float m_fade = 0.0F;     // seconds into the cross-fade
  float m_fadeTime = 0.0F; // total cross-fade seconds
  float m_speed = 1.0F;

  int m_overlay = -1;
  float m_overlayTime = 0.0F;
  float m_overlayFade = 0.1F;
  bool m_overlayLoop = false;
  std::vector<float> m_overlayMask;

  Pose m_pose;
  Pose m_scratchA;
  Pose m_scratchB;
};
} // namespace Paper
