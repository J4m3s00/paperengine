#include "core/assets/image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include <fstream>
#include <iterator>
#include <string>

namespace Paper {

std::expected<Image, Error> decodeImage(std::span<const std::uint8_t> bytes) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc *decoded =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                            &width, &height, &channels, STBI_rgb_alpha);
  if (decoded == nullptr) {
    return std::unexpected(Error{
        ErrorCode::Io, std::string("Failed to decode image: ") +
                           stbi_failure_reason()});
  }
  Image image;
  image.width = static_cast<std::uint32_t>(width);
  image.height = static_cast<std::uint32_t>(height);
  image.pixels.assign(decoded, decoded + static_cast<std::size_t>(width) *
                                             static_cast<std::size_t>(height) * 4);
  stbi_image_free(decoded);
  return image;
}

std::expected<Image, Error> loadImage(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::unexpected(
        Error{ErrorCode::Io, "Cannot open image file: " + path.string()});
  }
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
  auto image = decodeImage(bytes);
  if (!image) {
    image.error().message += " (" + path.string() + ")";
  }
  return image;
}
} // namespace Paper
