#include <doctest/doctest.h>

#include "anim/animator.h"

#include <glm/gtc/constants.hpp>

using namespace Paper;
using glm::vec3;
using glm::vec4;

namespace {
// root -> arm -> hand, each one unit up from its parent.
AnimationLibrary makeLibrary() {
  AnimationLibrary lib;
  const auto joint = [](const char *name, int parent, vec3 at) {
    Joint j;
    j.name = name;
    j.parent = parent;
    j.bind.translation = at;
    return j;
  };
  lib.skeleton.joints = {joint("root", -1, {0, 0, 0}), joint("arm", 0, {0, 1, 0}),
                         joint("hand", 1, {0, 1, 0})};
  // Inverse bind: undo the rest pose translation of each joint in model space.
  lib.skeleton.joints[0].inverseBind = glm::mat4(1.0F);
  lib.skeleton.joints[1].inverseBind = glm::translate(glm::mat4(1.0F), vec3(0, -1, 0));
  lib.skeleton.joints[2].inverseBind = glm::translate(glm::mat4(1.0F), vec3(0, -2, 0));

  AnimationClip slide;
  slide.name = "Slide_Loop";
  slide.duration = 2.0F;
  slide.loops = true;
  AnimChannel move;
  move.joint = 0;
  move.path = AnimChannel::Path::Translation;
  move.times = {0.0F, 2.0F};
  move.values = {vec4(0, 0, 0, 0), vec4(10, 0, 0, 0)};
  slide.channels.push_back(move);
  lib.clips.push_back(slide);

  AnimationClip bend;
  bend.name = "Bend";
  bend.duration = 1.0F;
  AnimChannel rot;
  rot.joint = 1;
  rot.path = AnimChannel::Path::Rotation;
  const glm::quat quarter = glm::angleAxis(glm::half_pi<float>(), vec3(0, 0, 1));
  rot.times = {0.0F, 1.0F};
  rot.values = {vec4(0, 0, 0, 1), vec4(quarter.x, quarter.y, quarter.z, quarter.w)};
  bend.channels.push_back(rot);
  lib.clips.push_back(bend);
  return lib;
}
} // namespace

TEST_CASE("the bind pose reproduces the rest skeleton and skins to identity") {
  const AnimationLibrary lib = makeLibrary();
  REQUIRE(lib.skeleton.valid());
  const Pose pose = bindPose(lib.skeleton);
  std::vector<glm::mat4> world;
  std::vector<glm::mat4> skin;
  jointWorldMatrices(lib.skeleton, pose, world);
  CHECK(vec3(world[2][3]) == vec3(0, 2, 0));
  skinMatrices(lib.skeleton, world, skin);
  for (const glm::mat4 &m : skin) {
    CHECK(glm::distance(m[3], vec4(0, 0, 0, 1)) < 1e-5F);
    CHECK(m[0][0] == doctest::Approx(1.0F));
  }
}

TEST_CASE("clips interpolate between keys, loop and clamp") {
  const AnimationLibrary lib = makeLibrary();
  Pose pose;
  sampleClip(lib.skeleton, lib.clips[0], 1.0F, pose);
  CHECK(pose[0].translation.x == doctest::Approx(5.0F));
  sampleClip(lib.skeleton, lib.clips[0], 3.0F, pose); // loops: 3 s == 1 s
  CHECK(pose[0].translation.x == doctest::Approx(5.0F));
  sampleClip(lib.skeleton, lib.clips[0], -0.5F, pose); // negative wraps too
  CHECK(pose[0].translation.x == doctest::Approx(7.5F));
  sampleClip(lib.skeleton, lib.clips[1], 5.0F, pose); // one-shot clamps at the end
  const vec3 hand = vec3(glm::mat4_cast(pose[1].rotation) * vec4(0, 1, 0, 0));
  CHECK(hand.x == doctest::Approx(-1.0F).epsilon(1e-4));
  // Joints a clip does not touch stay in the rest pose.
  CHECK(pose[2].translation == vec3(0, 1, 0));
}

TEST_CASE("rotations take the short way and stay normalized") {
  const AnimationLibrary lib = makeLibrary();
  Pose pose;
  sampleClip(lib.skeleton, lib.clips[1], 0.5F, pose);
  CHECK(glm::length(pose[1].rotation) == doctest::Approx(1.0F));
  CHECK(glm::angle(pose[1].rotation) == doctest::Approx(glm::quarter_pi<float>()).epsilon(1e-3));
}

TEST_CASE("a rotated arm moves the hand and the skinning matrix agrees") {
  const AnimationLibrary lib = makeLibrary();
  Pose pose;
  sampleClip(lib.skeleton, lib.clips[1], 1.0F, pose);
  std::vector<glm::mat4> world;
  std::vector<glm::mat4> skin;
  jointWorldMatrices(lib.skeleton, pose, world);
  skinMatrices(lib.skeleton, world, skin);
  // The hand's rest position (0,2,0) rotated 90 degrees about the arm at (0,1,0).
  CHECK(vec3(world[2][3]).x == doctest::Approx(-1.0F).epsilon(1e-4));
  CHECK(vec3(world[2][3]).y == doctest::Approx(1.0F).epsilon(1e-4));
  // A vertex at the rest hand position follows the hand.
  const vec3 moved = vec3(skin[2] * vec4(0, 2, 0, 1));
  CHECK(moved.x == doctest::Approx(-1.0F).epsilon(1e-4));
  CHECK(moved.y == doctest::Approx(1.0F).epsilon(1e-4));
}

TEST_CASE("the root offset places the whole skeleton") {
  AnimationLibrary lib = makeLibrary();
  lib.skeleton.rootOffset = glm::translate(glm::mat4(1.0F), vec3(5, 0, 0));
  std::vector<glm::mat4> world;
  jointWorldMatrices(lib.skeleton, bindPose(lib.skeleton), world);
  CHECK(vec3(world[0][3]) == vec3(5, 0, 0));
  CHECK(vec3(world[2][3]) == vec3(5, 2, 0));
}

TEST_CASE("blending and masks") {
  const AnimationLibrary lib = makeLibrary();
  Pose a;
  Pose b;
  Pose out;
  sampleClip(lib.skeleton, lib.clips[0], 0.0F, a);
  sampleClip(lib.skeleton, lib.clips[0], 2.0F - 1e-4F, b);
  blendPoses(a, b, 0.5F, out);
  CHECK(out[0].translation.x == doctest::Approx(5.0F).epsilon(1e-3));
  const auto mask = maskBelow(lib.skeleton, "arm");
  CHECK(mask == std::vector<float>{0.0F, 1.0F, 1.0F});
  CHECK(maskBelow(lib.skeleton, "nope") == std::vector<float>{0, 0, 0});
  Pose base = bindPose(lib.skeleton);
  Pose over;
  sampleClip(lib.skeleton, lib.clips[1], 1.0F, over);
  overlayPose(base, over, mask);
  CHECK(glm::angle(base[1].rotation) == doctest::Approx(glm::half_pi<float>()).epsilon(1e-3));
  CHECK(glm::angle(base[0].rotation) < 1e-4F); // root untouched
}

TEST_CASE("the animator advances, loops and cross-fades") {
  const AnimationLibrary lib = makeLibrary();
  Animator anim(lib);
  anim.play(0);
  anim.update(1.0F);
  CHECK(anim.pose()[0].translation.x == doctest::Approx(5.0F));
  anim.update(2.0F); // time 3 s: loops
  CHECK(anim.pose()[0].translation.x == doctest::Approx(5.0F));
  // Cross-fade into the bend: halfway through the fade the arm is half bent.
  anim.play(1, 1.0F);
  anim.update(0.5F);
  CHECK(glm::angle(anim.pose()[1].rotation) > 0.0F);
  CHECK(glm::angle(anim.pose()[1].rotation) < glm::half_pi<float>() * 0.5F);
  anim.update(2.0F); // fade done, clip ended
  CHECK(anim.current() == 1);
  CHECK(anim.finished());
  CHECK(glm::angle(anim.pose()[1].rotation) == doctest::Approx(glm::half_pi<float>()).epsilon(1e-3));
  // Playing the same clip again is a no-op; restart rewinds it.
  anim.play(1);
  CHECK(anim.time() > 1.0F);
  anim.play(1, 0.0F, true);
  CHECK(anim.time() == 0.0F);
}

TEST_CASE("an overlay bends only the masked joints and fades out") {
  const AnimationLibrary lib = makeLibrary();
  Animator anim(lib);
  anim.play(0);
  anim.update(0.0F);
  anim.playOverlay(1, maskBelow(lib.skeleton, "arm"), 0.1F);
  anim.update(0.5F);
  CHECK(anim.overlayActive());
  CHECK(glm::angle(anim.pose()[1].rotation) > 0.1F);
  CHECK(glm::angle(anim.pose()[0].rotation) < 1e-4F);
  anim.update(1.0F); // overlay clip (1 s) is over
  CHECK_FALSE(anim.overlayActive());
  CHECK(glm::angle(anim.pose()[1].rotation) < 1e-4F);
}

TEST_CASE("switching locomotion keeps the phase") {
  AnimationLibrary lib = makeLibrary();
  lib.clips.push_back(lib.clips[0]);
  lib.clips[2].name = "Slide2_Loop";
  lib.clips[2].duration = 4.0F;
  Animator anim(lib);
  anim.play(0);
  anim.update(1.0F); // half way through a 2 s clip
  anim.playKeepingPhase(2, 0.0F);
  CHECK(anim.time() == doctest::Approx(2.0F)); // half way through the 4 s clip
}

TEST_CASE("invalid clips are ignored safely") {
  const AnimationLibrary lib = makeLibrary();
  Animator anim(lib);
  anim.play(99);
  anim.play(-1);
  CHECK(anim.current() == -1);
  anim.update(1.0F);
  CHECK(anim.pose().size() == 3);
  std::vector<glm::mat4> skin;
  anim.computeMatrices(skin);
  CHECK(skin.size() == 3);
  CHECK(lib.findClip("Bend") == 1);
  CHECK(lib.findClip("nope") == -1);
}

TEST_CASE("a looping overlay keeps playing until replaced or stopped") {
  const AnimationLibrary lib = makeLibrary();
  Animator anim(lib);
  anim.play(0);
  anim.playOverlay(1, maskBelow(lib.skeleton, "arm"), 0.1F, true);
  anim.update(0.5F);
  anim.update(5.0F); // far past the 1 s clip
  CHECK(anim.overlayActive());
  CHECK(anim.overlayClip() == 1);
  CHECK(glm::angle(anim.pose()[1].rotation) > 0.0F);
  anim.playOverlay(-1, {});
  CHECK_FALSE(anim.overlayActive());
}
