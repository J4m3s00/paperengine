#pragma once

#include "base/error.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

namespace Paper {
using SoundId = std::uint32_t;

struct AudioConfig {
  // No sound device: mixing only happens in `render()` (tests, servers).
  bool noDevice = false;
  std::uint32_t sampleRate = 48000;
};

// Fire-and-forget 3D one-shots with a fixed voice pool. Units are Source units
// (1 inch); sounds fade with distance and pan by direction. Not thread safe:
// call from the game thread.
class AudioEngine {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<AudioEngine>, Error>
  create(const AudioConfig &config = {});
  ~AudioEngine();
  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;

  // Registers mono float samples (-1..1) recorded at `sampleRate`.
  [[nodiscard]] SoundId addSound(std::span<const float> mono, std::uint32_t sampleRate);

  void setListener(const glm::vec3 &position, const glm::vec3 &forward,
                   const glm::vec3 &up = {0.0F, 1.0F, 0.0F});
  // Plays at a world position. Returns false if all voices are busy.
  bool play(SoundId sound, const glm::vec3 &position, float volume = 1.0F, float pitch = 1.0F);
  // Plays without positioning (UI, own weapon).
  bool play2D(SoundId sound, float volume = 1.0F, float pitch = 1.0F);

  [[nodiscard]] std::uint32_t sampleRate() const;
  [[nodiscard]] int activeVoices() const;
  // With `noDevice`: mixes `frames` stereo frames into `interleaved`
  // (2 * frames floats).
  void render(std::span<float> interleaved);

private:
  struct Impl;
  explicit AudioEngine(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};
} // namespace Paper
