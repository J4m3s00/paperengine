#include "anim/animator.h"

#include <algorithm>
#include <cmath>

namespace Paper {
Pose bindPose(const Skeleton &skeleton) {
  Pose pose(skeleton.joints.size());
  for (std::size_t i = 0; i < pose.size(); ++i) {
    pose[i] = skeleton.joints[i].bind;
  }
  return pose;
}

namespace {
// Index of the last key at or before `time` (0 if before the first).
std::size_t keyBefore(const std::vector<float> &times, float time) {
  const auto it = std::upper_bound(times.begin(), times.end(), time);
  return it == times.begin() ? 0 : static_cast<std::size_t>(it - times.begin()) - 1;
}

glm::vec4 sampleChannel(const AnimChannel &ch, float time, bool rotation) {
  const std::size_t n = ch.times.size();
  if (n == 1 || time <= ch.times.front()) {
    return ch.values.front();
  }
  if (time >= ch.times.back()) {
    return ch.values.back();
  }
  const std::size_t i = keyBefore(ch.times, time);
  if (ch.step) {
    return ch.values[i];
  }
  const float span = ch.times[i + 1] - ch.times[i];
  const float t = span > 0.0F ? (time - ch.times[i]) / span : 0.0F;
  if (rotation) {
    const glm::quat a(ch.values[i].w, ch.values[i].x, ch.values[i].y, ch.values[i].z);
    const glm::quat b(ch.values[i + 1].w, ch.values[i + 1].x, ch.values[i + 1].y,
                      ch.values[i + 1].z);
    const glm::quat q = glm::slerp(a, b, t);
    return {q.x, q.y, q.z, q.w};
  }
  return glm::mix(ch.values[i], ch.values[i + 1], t);
}
} // namespace

void sampleClip(const Skeleton &skeleton, const AnimationClip &clip, float time, Pose &out) {
  if (out.size() != skeleton.joints.size()) {
    out = bindPose(skeleton);
  } else {
    for (std::size_t i = 0; i < out.size(); ++i) {
      out[i] = skeleton.joints[i].bind;
    }
  }
  if (clip.duration > 0.0F) {
    time = clip.loops ? std::fmod(std::fmod(time, clip.duration) + clip.duration, clip.duration)
                      : std::clamp(time, 0.0F, clip.duration);
  } else {
    time = 0.0F;
  }
  for (const AnimChannel &ch : clip.channels) {
    if (ch.joint < 0 || static_cast<std::size_t>(ch.joint) >= out.size() || ch.times.empty() ||
        ch.values.size() != ch.times.size()) {
      continue;
    }
    JointTransform &j = out[static_cast<std::size_t>(ch.joint)];
    switch (ch.path) {
    case AnimChannel::Path::Translation: {
      const glm::vec4 v = sampleChannel(ch, time, false);
      j.translation = {v.x, v.y, v.z};
      break;
    }
    case AnimChannel::Path::Rotation: {
      const glm::vec4 v = sampleChannel(ch, time, true);
      j.rotation = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
      break;
    }
    case AnimChannel::Path::Scale: {
      const glm::vec4 v = sampleChannel(ch, time, false);
      j.scale = {v.x, v.y, v.z};
      break;
    }
    }
  }
}

namespace {
JointTransform mixJoint(const JointTransform &a, const JointTransform &b, float t) {
  JointTransform r;
  r.translation = glm::mix(a.translation, b.translation, t);
  r.scale = glm::mix(a.scale, b.scale, t);
  // Take the short way round, then normalized lerp.
  glm::quat q = b.rotation;
  if (glm::dot(a.rotation, q) < 0.0F) {
    q = -q;
  }
  r.rotation = glm::normalize(glm::mix(a.rotation, q, t));
  return r;
}
} // namespace

void blendPoses(const Pose &a, const Pose &b, float t, Pose &out) {
  out.resize(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    out[i] = mixJoint(a[i], b[i], t);
  }
}

void overlayPose(Pose &base, const Pose &over, const std::vector<float> &weights) {
  for (std::size_t i = 0; i < base.size() && i < weights.size(); ++i) {
    if (weights[i] > 0.0F) {
      base[i] = mixJoint(base[i], over[i], weights[i]);
    }
  }
}

std::vector<float> maskBelow(const Skeleton &skeleton, std::string_view rootJoint) {
  std::vector<float> mask(skeleton.joints.size(), 0.0F);
  const int root = skeleton.find(rootJoint);
  if (root < 0) {
    return mask;
  }
  for (std::size_t i = 0; i < mask.size(); ++i) {
    int j = static_cast<int>(i);
    while (j >= 0 && j != root) {
      j = skeleton.joints[static_cast<std::size_t>(j)].parent;
    }
    mask[i] = j == root ? 1.0F : 0.0F;
  }
  return mask;
}

void jointWorldMatrices(const Skeleton &skeleton, const Pose &pose, std::vector<glm::mat4> &world) {
  world.resize(skeleton.joints.size());
  for (std::size_t i = 0; i < world.size(); ++i) {
    const int parent = skeleton.joints[i].parent;
    const glm::mat4 &parentMatrix =
        parent < 0 ? skeleton.rootOffset : world[static_cast<std::size_t>(parent)];
    world[i] = parentMatrix * pose[i].matrix();
  }
}

void skinMatrices(const Skeleton &skeleton, const std::vector<glm::mat4> &world,
                  std::vector<glm::mat4> &out) {
  out.resize(world.size());
  for (std::size_t i = 0; i < world.size(); ++i) {
    out[i] = world[i] * skeleton.joints[i].inverseBind;
  }
}

Animator::Animator(const AnimationLibrary &library)
    : m_library(&library), m_pose(bindPose(library.skeleton)) {}

void Animator::play(int clip, float fadeSeconds, bool restart) {
  if (clip < 0 || static_cast<std::size_t>(clip) >= m_library->clips.size()) {
    return;
  }
  if (clip == m_current && !restart) {
    return;
  }
  if (m_current >= 0 && fadeSeconds > 0.0F) {
    m_previous = m_current;
    m_previousTime = m_time;
    m_fade = 0.0F;
    m_fadeTime = fadeSeconds;
  } else {
    m_previous = -1;
  }
  m_current = clip;
  m_time = 0.0F;
}

void Animator::playKeepingPhase(int clip, float fadeSeconds) {
  if (clip == m_current || m_current < 0 || clip < 0 ||
      static_cast<std::size_t>(clip) >= m_library->clips.size()) {
    play(clip, fadeSeconds);
    return;
  }
  const AnimationClip &from = m_library->clips[static_cast<std::size_t>(m_current)];
  const AnimationClip &to = m_library->clips[static_cast<std::size_t>(clip)];
  const float phase = from.duration > 0.0F ? std::fmod(m_time, from.duration) / from.duration : 0.0F;
  play(clip, fadeSeconds);
  m_time = phase * to.duration;
}

void Animator::playOverlay(int clip, std::vector<float> mask, float fadeSeconds, bool loop) {
  if (clip < 0 || static_cast<std::size_t>(clip) >= m_library->clips.size()) {
    m_overlay = -1;
    return;
  }
  m_overlay = clip;
  m_overlayTime = 0.0F;
  m_overlayFade = std::max(fadeSeconds, 1e-3F);
  m_overlayLoop = loop;
  m_overlayMask = std::move(mask);
}

bool Animator::finished() const {
  if (m_current < 0) {
    return true;
  }
  const AnimationClip &c = m_library->clips[static_cast<std::size_t>(m_current)];
  return !c.loops && m_time >= c.duration;
}

void Animator::update(float dt) {
  const Skeleton &sk = m_library->skeleton;
  if (m_current < 0) {
    m_pose = bindPose(sk);
    return;
  }
  m_time += dt * m_speed;
  sampleClip(sk, m_library->clips[static_cast<std::size_t>(m_current)], m_time, m_pose);
  if (m_previous >= 0) {
    m_previousTime += dt * m_speed;
    m_fade += dt;
    if (m_fade >= m_fadeTime) {
      m_previous = -1;
    } else {
      sampleClip(sk, m_library->clips[static_cast<std::size_t>(m_previous)], m_previousTime, m_scratchA);
      blendPoses(m_scratchA, m_pose, m_fade / m_fadeTime, m_pose);
    }
  }
  if (m_overlay >= 0) {
    const AnimationClip &oc = m_library->clips[static_cast<std::size_t>(m_overlay)];
    m_overlayTime += dt;
    if (!m_overlayLoop && m_overlayTime >= oc.duration) {
      m_overlay = -1;
    } else {
      // A looping overlay wraps (the clip's own loop flag does not matter).
      const float t = m_overlayLoop && oc.duration > 0.0F ? std::fmod(m_overlayTime, oc.duration)
                                                          : m_overlayTime;
      sampleClip(sk, oc, t, m_scratchB);
      // Fade in at the start and out at the end of a one-shot overlay.
      const float in = std::min(1.0F, m_overlayTime / m_overlayFade);
      const float out = m_overlayLoop ? 1.0F : std::min(1.0F, (oc.duration - m_overlayTime) / m_overlayFade);
      const float w = std::min(in, out);
      std::vector<float> weights(m_overlayMask);
      for (float &v : weights) {
        v *= w;
      }
      overlayPose(m_pose, m_scratchB, weights);
    }
  }
}

void Animator::computeMatrices(std::vector<glm::mat4> &skin, std::vector<glm::mat4> *world) const {
  std::vector<glm::mat4> localWorld;
  std::vector<glm::mat4> &w = world != nullptr ? *world : localWorld;
  jointWorldMatrices(m_library->skeleton, m_pose, w);
  skinMatrices(m_library->skeleton, w, skin);
}
} // namespace Paper
