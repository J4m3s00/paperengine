#pragma once

#include "anim/clip.h"

#include <vector>

namespace Paper {
// The rest pose.
[[nodiscard]] Pose bindPose(const Skeleton &skeleton);

// Samples `clip` at `time` (wrapped when the clip loops, clamped otherwise)
// into `out`. Joints the clip does not animate keep their rest pose.
void sampleClip(const Skeleton &skeleton, const AnimationClip &clip, float time, Pose &out);

// out = a * (1 - t) + b * t per joint (normalized lerp for rotations).
void blendPoses(const Pose &a, const Pose &b, float t, Pose &out);
// base = base * (1 - w) + over * w, with a weight per joint.
void overlayPose(Pose &base, const Pose &over, const std::vector<float> &weights);

// Weight 1 for `rootJoint` and everything below it, 0 elsewhere (e.g. the
// upper body from "spine_01").
[[nodiscard]] std::vector<float> maskBelow(const Skeleton &skeleton, std::string_view rootJoint);

// Model-space matrix of every joint (without the inverse bind).
void jointWorldMatrices(const Skeleton &skeleton, const Pose &pose, std::vector<glm::mat4> &world);
// Skinning matrices: world * inverseBind, what the shader multiplies vertices by.
void skinMatrices(const Skeleton &skeleton, const std::vector<glm::mat4> &world,
                  std::vector<glm::mat4> &out);
} // namespace Paper
