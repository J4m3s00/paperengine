#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <string_view>
#include <vector>

// Skeletal animation data. Units are whatever the asset uses (meters for the
// glTF characters); the game scales the whole model.
namespace Paper {
struct JointTransform {
  glm::vec3 translation{0.0F};
  glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  glm::vec3 scale{1.0F};

  [[nodiscard]] glm::mat4 matrix() const {
    return glm::translate(glm::mat4(1.0F), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0F), scale);
  }
};

struct Joint {
  std::string name;
  // Index of the parent joint, -1 for a root. Parents come before children.
  int parent = -1;
  // Rest pose relative to the parent.
  JointTransform bind;
  // Takes a vertex from model space into this joint's space in the rest pose.
  glm::mat4 inverseBind{1.0F};
};

struct Skeleton {
  std::vector<Joint> joints;
  // Where the root joints' parent sits in model space (the armature node and
  // the skinned mesh node of the source file folded together).
  glm::mat4 rootOffset{1.0F};

  [[nodiscard]] int find(std::string_view name) const {
    for (std::size_t i = 0; i < joints.size(); ++i) {
      if (joints[i].name == name) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
  [[nodiscard]] bool valid() const {
    for (std::size_t i = 0; i < joints.size(); ++i) {
      if (joints[i].parent >= static_cast<int>(i)) {
        return false;
      }
    }
    return !joints.empty();
  }
};

// Local transform of every joint.
using Pose = std::vector<JointTransform>;
} // namespace Paper
