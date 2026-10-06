#include "core/window.h"

#include "GLFW/glfw3.h"
#include <utility>
#include <string_view>

namespace Paper {
namespace {

int g_glfwUsers = 0;

void glfwErrorCallback(int code, const char *description) {
  // Swap for spdlog once your logging wrapper exists.
  (void)code;
  (void)description;
}

std::expected<void, Error> acquireGlfw() {
  if (g_glfwUsers == 0) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == GLFW_FALSE) {
      return std::unexpected(
          Error{ErrorCode::Window, "Failed to initialize GLFW"});
    }
  }
  ++g_glfwUsers;
  return {};
}

void releaseGlfw() {
  if (--g_glfwUsers == 0) {
    glfwTerminate();
  }
}
} // namespace

struct Window::Impl {
  GLFWwindow *handle;

  // Takes over one GLFW reference acquired by the caller.
  explicit Impl(GLFWwindow *window) : handle(window) {}

  ~Impl() {
    glfwDestroyWindow(handle);
    releaseGlfw();
  }

  Impl(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl &operator=(Impl &&) = delete;
};

Window::Window(std::unique_ptr<Impl> impl) : m_pImpl(std::move(impl)) {}

std::expected<std::unique_ptr<Window>, Error>
Window::create(const WindowConfig &config) {
  if (auto acquired = acquireGlfw(); !acquired) {
    return std::unexpected(acquired.error());
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *handle = glfwCreateWindow(
      config.width, config.height, config.title.c_str(), nullptr, nullptr);
  if (handle == nullptr) {
    releaseGlfw();
    return std::unexpected(
        Error{ErrorCode::Window, "Failed to create GLFW window"});
  }
  return std::unique_ptr<Window>(
      new Window(std::make_unique<Impl>(handle)));
}

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