#include <doctest/doctest.h>

#include "audio/audio.h"
#include "audio/synth.h"

#include <cmath>

using namespace Paper;

namespace {
std::unique_ptr<AudioEngine> makeEngine() {
  auto engine = AudioEngine::create({.noDevice = true});
  REQUIRE(engine.has_value());
  return std::move(*engine);
}

struct Loudness {
  float left = 0.0F;
  float right = 0.0F;
};

// RMS of each channel over `frames` frames.
Loudness mix(AudioEngine &engine, std::size_t frames) {
  std::vector<float> out(frames * 2);
  engine.render(out);
  Loudness l;
  for (std::size_t i = 0; i < frames; ++i) {
    l.left += out[i * 2] * out[i * 2];
    l.right += out[i * 2 + 1] * out[i * 2 + 1];
  }
  l.left = std::sqrt(l.left / static_cast<float>(frames));
  l.right = std::sqrt(l.right / static_cast<float>(frames));
  return l;
}

std::vector<float> tone(std::uint32_t rate, float seconds) {
  std::vector<float> s(static_cast<std::size_t>(seconds * static_cast<float>(rate)));
  for (std::size_t i = 0; i < s.size(); ++i) {
    s[i] = 0.5F * std::sin(0.05F * static_cast<float>(i));
  }
  return s;
}
} // namespace

TEST_CASE("synthesized sounds are finite, audible and bounded") {
  using namespace Synth;
  for (const auto &s : {shot(ShotKind::Rifle, 48000), shot(ShotKind::Pistol, 48000),
                        shot(ShotKind::Sniper, 48000), shot(ShotKind::Knife, 48000),
                        footstep(48000), impact(48000), hitMarker(48000), reload(48000)}) {
    REQUIRE_FALSE(s.empty());
    float peak = 0.0F;
    for (const float v : s) {
      REQUIRE(std::isfinite(v));
      peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak > 0.05F);
    CHECK(peak <= 1.0F);
  }
  CHECK(shot(ShotKind::Sniper, 48000).size() > shot(ShotKind::Pistol, 48000).size());
  CHECK(shot(ShotKind::Rifle, 48000) == shot(ShotKind::Rifle, 48000));
}

TEST_CASE("silence without sounds, then a played sound is heard and ends") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(48000, 0.1F), 48000);
  engine->setListener({0, 0, 0}, {0, 0, -1});
  CHECK(mix(*engine, 256).left == 0.0F);
  REQUIRE(engine->play2D(id));
  CHECK(engine->activeVoices() == 1);
  const Loudness l = mix(*engine, 1024);
  CHECK(l.left > 0.05F);
  CHECK(l.right > 0.05F);
  mix(*engine, 48000); // let it finish
  CHECK(engine->activeVoices() == 0);
}

TEST_CASE("3D sounds pan toward the side they come from") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(48000, 0.3F), 48000);
  engine->setListener({0, 0, 0}, {0, 0, -1});
  REQUIRE(engine->play(id, {-300, 0, 0})); // to the listener's left
  const Loudness left = mix(*engine, 2048);
  CHECK(left.left > left.right * 1.5F);
  mix(*engine, 48000);
  REQUIRE(engine->play(id, {300, 0, 0}));
  const Loudness right = mix(*engine, 2048);
  CHECK(right.right > right.left * 1.5F);
}

TEST_CASE("turning the listener moves the sound across the stereo field") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(48000, 0.3F), 48000);
  // A sound straight ahead (-Z); the listener then faces +X, so it is on the left.
  engine->setListener({0, 0, 0}, {1, 0, 0});
  REQUIRE(engine->play(id, {0, 0, -300}));
  const Loudness l = mix(*engine, 2048);
  CHECK(l.left > l.right * 1.5F);
}

TEST_CASE("sounds get quieter with distance") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(48000, 0.3F), 48000);
  engine->setListener({0, 0, 0}, {0, 0, -1});
  REQUIRE(engine->play(id, {0, 0, -200}));
  const Loudness near = mix(*engine, 2048);
  mix(*engine, 48000);
  REQUIRE(engine->play(id, {0, 0, -2000}));
  const Loudness far = mix(*engine, 2048);
  CHECK(far.left < near.left * 0.5F);
  CHECK(far.left > 0.0F);
}

TEST_CASE("the voice pool is bounded and recycles finished voices") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(48000, 0.05F), 48000);
  int played = 0;
  for (int i = 0; i < 100; ++i) {
    played += engine->play2D(id) ? 1 : 0;
  }
  CHECK(played == 32);
  mix(*engine, 48000);
  CHECK(engine->play2D(id));
}

TEST_CASE("sounds at another sample rate are resampled to the engine rate") {
  auto engine = makeEngine();
  const SoundId id = engine->addSound(tone(24000, 0.1F), 24000);
  REQUIRE(engine->play2D(id));
  mix(*engine, 4000); // 83 ms: still playing
  CHECK(engine->activeVoices() == 1);
  mix(*engine, 4000);
  CHECK(engine->activeVoices() == 0);
}
