#include "audio/synth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Paper::Synth {
namespace {
struct Noise {
  std::uint32_t state = 0x12345678U;
  float next() {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state >> 8) / 8388608.0F - 1.0F;
  }
};

// Noise through a one-pole low-pass, shaped by an exponential decay, plus a
// sine thump: the recipe for most of the sounds here.
std::vector<float> burst(std::uint32_t rate, float seconds, float decay, float lowpass,
                         float thumpHz, float thumpDecay, float thumpGain, float noiseGain) {
  Noise noise;
  std::vector<float> out(static_cast<std::size_t>(seconds * static_cast<float>(rate)));
  float filtered = 0.0F;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(rate);
    filtered += (noise.next() - filtered) * lowpass;
    const float n = filtered * std::exp(-t * decay) * noiseGain;
    const float thump = std::sin(2.0F * std::numbers::pi_v<float> * thumpHz * t) *
                        std::exp(-t * thumpDecay) * thumpGain;
    // A few samples of fade-in avoid a click at the start.
    const float attack = std::min(1.0F, static_cast<float>(i) / 16.0F);
    out[i] = std::clamp((n + thump) * attack, -1.0F, 1.0F);
  }
  return out;
}
} // namespace

std::vector<float> shot(ShotKind kind, std::uint32_t rate) {
  switch (kind) {
  case ShotKind::Rifle: return burst(rate, 0.30F, 22.0F, 0.45F, 85.0F, 30.0F, 0.6F, 1.6F);
  case ShotKind::Pistol: return burst(rate, 0.20F, 35.0F, 0.6F, 140.0F, 45.0F, 0.4F, 1.3F);
  case ShotKind::Sniper: return burst(rate, 0.80F, 9.0F, 0.25F, 55.0F, 12.0F, 0.9F, 1.8F);
  case ShotKind::Knife: return burst(rate, 0.15F, 30.0F, 0.9F, 0.0F, 1.0F, 0.0F, 0.5F);
  }
  return {};
}

std::vector<float> footstep(std::uint32_t rate) {
  return burst(rate, 0.12F, 45.0F, 0.12F, 70.0F, 55.0F, 0.5F, 1.0F);
}
std::vector<float> impact(std::uint32_t rate) {
  return burst(rate, 0.12F, 40.0F, 0.5F, 220.0F, 60.0F, 0.2F, 0.9F);
}
std::vector<float> hitMarker(std::uint32_t rate) {
  return burst(rate, 0.07F, 55.0F, 0.0F, 1500.0F, 50.0F, 0.5F, 0.0F);
}
std::vector<float> reload(std::uint32_t rate) {
  return burst(rate, 0.10F, 50.0F, 0.7F, 600.0F, 70.0F, 0.2F, 0.7F);
}
} // namespace Paper::Synth
