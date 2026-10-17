#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include "audio/audio.h"

#include <algorithm>
#include <vector>

namespace Paper {
namespace {
constexpr int kVoices = 32;
// Source units: full volume within a metre-ish, fading out to far away.
constexpr float kMinDistance = 100.0F;
constexpr float kMaxDistance = 6000.0F;
} // namespace

struct AudioEngine::Impl {
  struct Voice {
    ma_sound sound;
    ma_audio_buffer_ref buffer;
    bool initialized = false;
  };
  struct Sound {
    std::vector<float> samples;
  };

  ma_engine engine;
  bool engineReady = false;
  bool noDevice = false;
  std::vector<Sound> sounds;
  std::unique_ptr<Voice[]> voices = std::make_unique<Voice[]>(kVoices);

  ~Impl() {
    for (int i = 0; i < kVoices; ++i) {
      if (voices[i].initialized) {
        ma_sound_uninit(&voices[i].sound);
      }
    }
    if (engineReady) {
      ma_engine_uninit(&engine);
    }
  }

  Voice *freeVoice() {
    for (int i = 0; i < kVoices; ++i) {
      Voice &v = voices[i];
      if (!v.initialized) {
        return &v;
      }
      if (ma_sound_at_end(&v.sound)) {
        ma_sound_uninit(&v.sound);
        v.initialized = false;
        return &v;
      }
    }
    return nullptr;
  }

  bool start(SoundId id, bool spatial, const glm::vec3 &pos, float volume, float pitch) {
    if (id >= sounds.size()) {
      return false;
    }
    Voice *v = freeVoice();
    if (!v) {
      return false;
    }
    const std::vector<float> &data = sounds[id].samples;
    if (ma_audio_buffer_ref_init(ma_format_f32, 1, data.data(), data.size(), &v->buffer) !=
        MA_SUCCESS) {
      return false;
    }
    const ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&engine, &v->buffer, flags, nullptr, &v->sound) !=
        MA_SUCCESS) {
      return false;
    }
    v->initialized = true;
    ma_sound_set_volume(&v->sound, volume);
    ma_sound_set_pitch(&v->sound, pitch);
    if (spatial) {
      ma_sound_set_position(&v->sound, pos.x, pos.y, pos.z);
      ma_sound_set_attenuation_model(&v->sound, ma_attenuation_model_inverse);
      ma_sound_set_min_distance(&v->sound, kMinDistance);
      ma_sound_set_max_distance(&v->sound, kMaxDistance);
      ma_sound_set_doppler_factor(&v->sound, 0.0F);
    }
    return ma_sound_start(&v->sound) == MA_SUCCESS;
  }
};

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
AudioEngine::~AudioEngine() = default;

std::expected<std::unique_ptr<AudioEngine>, Error> AudioEngine::create(const AudioConfig &config) {
  auto impl = std::make_unique<Impl>();
  ma_engine_config cfg = ma_engine_config_init();
  cfg.channels = 2;
  cfg.sampleRate = config.sampleRate;
  cfg.noDevice = config.noDevice ? MA_TRUE : MA_FALSE;
  if (ma_engine_init(&cfg, &impl->engine) != MA_SUCCESS) {
    return std::unexpected(Error{ErrorCode::Io, "cannot start the audio engine (no sound device?)"});
  }
  impl->engineReady = true;
  impl->noDevice = config.noDevice;
  return std::unique_ptr<AudioEngine>(new AudioEngine(std::move(impl)));
}

SoundId AudioEngine::addSound(std::span<const float> mono, std::uint32_t sampleRate) {
  Impl::Sound sound;
  const std::uint32_t target = this->sampleRate();
  if (sampleRate == target || sampleRate == 0 || mono.empty()) {
    sound.samples.assign(mono.begin(), mono.end());
  } else {
    // Linear resampling to the engine rate keeps pitch and duration.
    const double ratio = static_cast<double>(sampleRate) / target;
    const auto n = static_cast<std::size_t>(static_cast<double>(mono.size()) / ratio);
    sound.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      const double pos = static_cast<double>(i) * ratio;
      const auto i0 = static_cast<std::size_t>(pos);
      const std::size_t i1 = std::min(i0 + 1, mono.size() - 1);
      const auto f = static_cast<float>(pos - static_cast<double>(i0));
      sound.samples[i] = mono[i0] * (1.0F - f) + mono[i1] * f;
    }
  }
  m_impl->sounds.push_back(std::move(sound));
  return static_cast<SoundId>(m_impl->sounds.size() - 1);
}

void AudioEngine::setListener(const glm::vec3 &position, const glm::vec3 &forward,
                              const glm::vec3 &up) {
  ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
  ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
  ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

bool AudioEngine::play(SoundId sound, const glm::vec3 &position, float volume, float pitch) {
  return m_impl->start(sound, true, position, volume, pitch);
}
bool AudioEngine::play2D(SoundId sound, float volume, float pitch) {
  return m_impl->start(sound, false, glm::vec3(0.0F), volume, pitch);
}

std::uint32_t AudioEngine::sampleRate() const { return ma_engine_get_sample_rate(&m_impl->engine); }

int AudioEngine::activeVoices() const {
  int active = 0;
  for (int i = 0; i < kVoices; ++i) {
    const Impl::Voice &v = m_impl->voices[i];
    active += v.initialized && !ma_sound_at_end(&v.sound) ? 1 : 0;
  }
  return active;
}

void AudioEngine::render(std::span<float> interleaved) {
  ma_uint64 read = 0;
  ma_engine_read_pcm_frames(&m_impl->engine, interleaved.data(), interleaved.size() / 2, &read);
}
} // namespace Paper
