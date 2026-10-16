#pragma once

#include <string>

namespace Paper {
enum class ErrorCode {
  Window,
  Vulkan,
  Swapchain,
  Io,
  Shader,
  InvalidArgument,
  Asset,
  NotFound,
};

struct Error {
  ErrorCode code;
  std::string message;
};
} // namespace Paper
