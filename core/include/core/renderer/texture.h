#pragma once

#include "core/handle.h"

namespace Paper {
// How the 8-bit texel values are interpreted. Colors (base color, emissive)
// are sRGB; data (normal, metallic-roughness, occlusion) is linear.
enum class ColorSpace {
  Srgb,
  Linear,
};

enum class Filter {
  Nearest,
  Linear,
};

enum class WrapMode {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
};

// Mipmaps are always generated and sampled; anisotropic filtering is on
// (4x, or the device maximum if lower).
struct SamplerSettings {
  Filter magFilter = Filter::Linear;
  Filter minFilter = Filter::Linear;
  WrapMode wrapU = WrapMode::Repeat;
  WrapMode wrapV = WrapMode::Repeat;

  friend auto operator<=>(const SamplerSettings &,
                          const SamplerSettings &) = default;
};

// Engine-owned fallbacks. Material texture slots start out with one of them:
// FlatNormal for slots whose name contains "normal" (case-insensitive), White
// for all others.
enum class DefaultTexture {
  White,
  FlatNormal,
};

struct TextureTag;
using TextureHandle = Handle<TextureTag>;
} // namespace Paper
