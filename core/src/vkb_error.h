#pragma once

#include "base/error.h"

#include "VkBootstrap.h"

#include <expected>
#include <string>
#include <utility>

namespace Paper {
// Translates a vk-bootstrap result into std::expected<T, Error>.
template <typename T>
std::expected<T, Error> unwrap(vkb::Result<T> result, ErrorCode code,
                               const char *what) {
  if (!result) {
    return std::unexpected(
        Error{code, std::string(what) + ": " + result.error().message()});
  }
  return std::move(result.value());
}
} // namespace Paper
