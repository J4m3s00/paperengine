#include "core/assets/image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Paper {

std::expected<void, Error> savePng(const std::filesystem::path &path,
                                   const Image &image) {
  if (image.width == 0 || image.height == 0 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                 "savePng: image must be non-empty RGBA8"});
  }
  const std::string pathString = path.string();
  if (stbi_write_png(pathString.c_str(), static_cast<int>(image.width),
                     static_cast<int>(image.height), 4, image.pixels.data(),
                     static_cast<int>(image.width) * 4) == 0) {
    return std::unexpected(
        Error{ErrorCode::Io, "Failed to write PNG: " + pathString});
  }
  return {};
}
} // namespace Paper
