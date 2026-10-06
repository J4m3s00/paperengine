#pragma once

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
  Window(const WindowConfig &config);
  ~Window();

  void setTitle(const std::string_view &title);
  [[nodiscard]] bool shouldClose() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] int framebufferWidth() const;
  [[nodiscard]] int framebufferHeight() const;
  [[nodiscard]] GLFWwindow *nativeHandle() const;

  void resize(int newWidth, int newHeight);
  void update();

private:
  struct Impl;
  std::unique_ptr<Impl> m_pImpl;
};
} // namespace Paper
