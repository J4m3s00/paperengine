#pragma once

#include "core/error.h"

#include <vulkan/vulkan.h>

#include <expected>
#include <string>

namespace Paper {
// Translates a VkResult into std::expected<void, Error>.
inline std::expected<void, Error> vkCheck(VkResult result, ErrorCode code,
                                          const char *what) {
  if (result != VK_SUCCESS) {
    return std::unexpected(Error{code, std::string(what) + " failed (VkResult " +
                                           std::to_string(result) + ")"});
  }
  return {};
}
} // namespace Paper
