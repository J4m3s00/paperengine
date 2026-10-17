#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Paper {

// Directory containing the running executable (empty if unknown).
[[nodiscard]] inline std::filesystem::path executableDir() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0) {
      return {};
    }
    if (n < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), n)).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return std::filesystem::path(buffer.data()).parent_path();
  }
  return {};
#elif defined(__linux__)
  std::error_code ec;
  const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path() : p.parent_path();
#else
  return {};
#endif
}

// Locates a data folder (`maps`, `weapons`, ...): `<exe dir>/<name>` when it
// exists (packaged release), otherwise `devFallback` (the source tree path
// baked in at configure time).
[[nodiscard]] inline std::filesystem::path dataDir(const char *name, const char *devFallback) {
  const std::filesystem::path exeDir = executableDir();
  if (!exeDir.empty() && std::filesystem::exists(exeDir / name)) {
    return exeDir / name;
  }
  return devFallback;
}

} // namespace Paper
