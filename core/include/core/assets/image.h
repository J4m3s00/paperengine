#pragma once

#include "core/error.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace Paper {
// Decoded 8-bit RGBA image, tightly packed, top row first.
struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

// Decodes PNG, JPEG and the other formats supported by stb_image. The result
// is always RGBA8.
[[nodiscard]] std::expected<Image, Error>
decodeImage(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::expected<Image, Error>
loadImage(const std::filesystem::path &path);
// Writes `image` as an 8-bit RGBA PNG.
[[nodiscard]] std::expected<void, Error>
savePng(const std::filesystem::path &path, const Image &image);
} // namespace Paper
