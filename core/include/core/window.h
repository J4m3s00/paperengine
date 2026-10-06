#pragma once

#include "core/error.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

struct GLFWwindow;

namespace Paper {
struct WindowConfig {
  int width;
  int height;
  std::string title;
};

class Window {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error>
  create(const WindowConfig &config);
  ~Window();

  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  Window(Window &&) = delete;
  Window &operator=(Window &&) = delete;

  void setTitle(const std::string_view &title);
  [[nodiscard]] bool shouldClose() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] int framebufferWidth() const;
  [[nodiscard]] int framebufferHeight() const;
  [[nodiscard]] GLFWwindow *nativeHandle() const;
  // Seconds since GLFW was initialised.
  [[nodiscard]] double time() const;

  void resize(int newWidth, int newHeight);
  void update();

private:
  struct Impl;
  explicit Window(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> m_pImpl;
};
} // namespace Paper
