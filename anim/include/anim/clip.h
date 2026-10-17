#pragma once

#include "anim/skeleton.h"

#include <string>
#include <vector>

namespace Paper {
struct AnimChannel {
  enum class Path { Translation, Rotation, Scale };
  int joint = -1;
  Path path = Path::Rotation;
  bool step = false; // hold each key instead of interpolating
  std::vector<float> times;
  // One value per key; vec3 channels use xyz, rotations are xyzw quaternions.
  std::vector<glm::vec4> values;
};

struct AnimationClip {
  std::string name;
  float duration = 0.0F;
  std::vector<AnimChannel> channels;
  // Loops by default when the name says so (Quaternius style "..._Loop").
  bool loops = false;
};

// A skeleton and everything that can animate it.
struct AnimationLibrary {
  Skeleton skeleton;
  std::vector<AnimationClip> clips;

  [[nodiscard]] int findClip(std::string_view name) const {
    for (std::size_t i = 0; i < clips.size(); ++i) {
      if (clips[i].name == name) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
};
} // namespace Paper
