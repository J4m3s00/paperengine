#include "core/window.h"

#include "GLFW/glfw3.h"
#include <stdexcept>
#include <string_view>

namespace Paper {
namespace {

int g_glfwUsers = 0;

void glfwErrorCallback(int code, const char *description) {
  // Swap for spdlog once your logging wrapper exists.
  (void)code;
  (void)description;
}

void acquireGlfw() {
  if (g_glfwUsers == 0) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE) {
      throw std::runtime_error("Failed to initialize GLFW");
    }
  }
  ++g_glfwUsers;
}

void releaseGlfw() {
  if (--g_glfwUsers == 0) {
    glfwTerminate();
  }
}
} // namespace

struct Window::Impl {
  GLFWwindow *handle;

  explicit Impl(const WindowConfig &config) {
    acquireGlfw();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    handle = glfwCreateWindow(config.width, config.height, config.title.c_str(),
                              nullptr, nullptr);
    if (handle == nullptr) {
      releaseGlfw();
      throw std::runtime_error("Failed to create GLFW window");
    }
  }

  ~Impl() {
    glfwDestroyWindow(handle);
    releaseGlfw();
  }

  Impl(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl &operator=(Impl &&) = delete;
};

Window::Window(const WindowConfig &config)
    : m_pImpl(std::make_unique<Impl>(config)) {}

Window::~Window() = default;

void Window::setTitle(const std::string_view &title) {
  const std::string copy{title};
  glfwSetWindowTitle(m_pImpl->handle, copy.c_str());
}

bool Window::shouldClose() const {
  return glfwWindowShouldClose(m_pImpl->handle) == GLFW_TRUE;
}

int Window::width() const {
  int w, h;
  glfwGetWindowSize(m_pImpl->handle, &w, &h);
  return w;
}

int Window::height() const {
  int w, h;
  glfwGetWindowSize(m_pImpl->handle, &w, &h);
  return h;
}

int Window::framebufferWidth() const {
  int w, h;
  glfwGetFramebufferSize(m_pImpl->handle, &w, &h);
  return w;
}

int Window::framebufferHeight() const {
  int w, h;
  glfwGetFramebufferSize(m_pImpl->handle, &w, &h);
  return h;
}

GLFWwindow *Window::nativeHandle() const { return m_pImpl->handle; }

void Window::resize(int newWidth, int newHeight) {
  glfwSetWindowSize(m_pImpl->handle, newWidth, newHeight);
}

void Window::update() { glfwPollEvents(); }

} // namespace Paper