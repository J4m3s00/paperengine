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

int toGlfw(Key key) {
  switch (key) {
  case Key::W:
    return GLFW_KEY_W;
  case Key::A:
    return GLFW_KEY_A;
  case Key::S:
    return GLFW_KEY_S;
  case Key::D:
    return GLFW_KEY_D;
  case Key::Q:
    return GLFW_KEY_Q;
  case Key::E:
    return GLFW_KEY_E;
  case Key::Space:
    return GLFW_KEY_SPACE;
  case Key::LeftShift:
    return GLFW_KEY_LEFT_SHIFT;
  case Key::LeftControl:
    return GLFW_KEY_LEFT_CONTROL;
  case Key::Escape:
    return GLFW_KEY_ESCAPE;
  case Key::F1:
    return GLFW_KEY_F1;
  case Key::R:
    return GLFW_KEY_R;
  case Key::T:
    return GLFW_KEY_T;
  case Key::Tab:
    return GLFW_KEY_TAB;
  case Key::Num1:
    return GLFW_KEY_1;
  case Key::Num2:
    return GLFW_KEY_2;
  case Key::Num3:
    return GLFW_KEY_3;
  case Key::Num4:
    return GLFW_KEY_4;
  case Key::Num5:
    return GLFW_KEY_5;
  case Key::Num6:
    return GLFW_KEY_6;
  case Key::Num7:
    return GLFW_KEY_7;
  }
  return GLFW_KEY_UNKNOWN;
}

int toGlfw(MouseButton button) {
  switch (button) {
  case MouseButton::Left:
    return GLFW_MOUSE_BUTTON_LEFT;
  case MouseButton::Right:
    return GLFW_MOUSE_BUTTON_RIGHT;
  case MouseButton::Middle:
    return GLFW_MOUSE_BUTTON_MIDDLE;
  }
  return GLFW_MOUSE_BUTTON_LEFT;
}
} // namespace

struct Window::Impl {
  GLFWwindow *handle;
  glm::dvec2 mousePosition{0.0};
  glm::dvec2 mouseDelta{0.0};
  glm::dvec2 scrollPending{0.0};
  glm::dvec2 scrollDelta{0.0};

  // Takes over one GLFW reference acquired by the caller.
  explicit Impl(GLFWwindow *window) : handle(window) {
    glfwGetCursorPos(handle, &mousePosition.x, &mousePosition.y);
    glfwSetWindowUserPointer(handle, this);
    glfwSetScrollCallback(handle, [](GLFWwindow *w, double x, double y) {
      static_cast<Impl *>(glfwGetWindowUserPointer(w))->scrollPending +=
          glm::dvec2(x, y);
    });
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

double Window::time() const { return glfwGetTime(); }

void Window::resize(int newWidth, int newHeight) {
  glfwSetWindowSize(m_pImpl->handle, newWidth, newHeight);
}

void Window::update() {
  const glm::dvec2 previous = m_pImpl->mousePosition;
  glfwPollEvents();
  glfwGetCursorPos(m_pImpl->handle, &m_pImpl->mousePosition.x,
                   &m_pImpl->mousePosition.y);
  m_pImpl->mouseDelta = m_pImpl->mousePosition - previous;
  m_pImpl->scrollDelta = m_pImpl->scrollPending;
  m_pImpl->scrollPending = glm::dvec2(0.0);
}

void Window::requestClose() {
  glfwSetWindowShouldClose(m_pImpl->handle, GLFW_TRUE);
}

bool Window::keyDown(Key key) const {
  return glfwGetKey(m_pImpl->handle, toGlfw(key)) == GLFW_PRESS;
}

bool Window::mouseDown(MouseButton button) const {
  return glfwGetMouseButton(m_pImpl->handle, toGlfw(button)) == GLFW_PRESS;
}

glm::dvec2 Window::mousePosition() const { return m_pImpl->mousePosition; }

glm::dvec2 Window::mouseDelta() const { return m_pImpl->mouseDelta; }

glm::dvec2 Window::scrollDelta() const { return m_pImpl->scrollDelta; }

void Window::setCursorMode(CursorMode mode) {
  glfwSetInputMode(m_pImpl->handle, GLFW_CURSOR,
                   mode == CursorMode::Captured ? GLFW_CURSOR_DISABLED
                                                : GLFW_CURSOR_NORMAL);
  glfwGetCursorPos(m_pImpl->handle, &m_pImpl->mousePosition.x,
                   &m_pImpl->mousePosition.y);
  m_pImpl->mouseDelta = glm::dvec2(0.0);
}

} // namespace Paper