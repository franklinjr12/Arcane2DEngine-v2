#include "arcane/window.hpp"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

#include <atomic>
#include <format>

#include "arcane/log.hpp"

namespace arcane {

namespace {
constexpr std::string_view kLogCategory = "window";
std::atomic<int> g_window_count{0};

void glfw_error_callback(int code, const char* description) {
  ARCANE_LOG_ERROR(kLogCategory, "GLFW error {}: {}", code, description);
}

void GLAPIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                   const GLchar* message, const void* /*user_param*/) {
  const auto level = (severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM)
                          ? LogLevel::Warn
                          : LogLevel::Trace;
  log(level, "gl", "source={} type={} id={} : {}", source, type, id, message);
}
}  // namespace

struct Window::Impl {
  GLFWwindow* window = nullptr;
};

std::expected<Window, std::string> Window::create(const WindowConfig& config) {
  if (g_window_count.fetch_add(1) == 0) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
      g_window_count.fetch_sub(1);
      return std::unexpected("glfwInit failed");
    }
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, config.hidden ? GLFW_FALSE : GLFW_TRUE);

  GLFWwindow* handle = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
  if (!handle) {
    if (g_window_count.fetch_sub(1) == 1) {
      glfwTerminate();
    }
    return std::unexpected("glfwCreateWindow failed");
  }

  glfwMakeContextCurrent(handle);
  glfwSwapInterval(config.vsync ? 1 : 0);

  Window window;
  window.impl_ = std::make_unique<Impl>();
  window.impl_->window = handle;
  return window;
}

Window::Window(Window&& other) noexcept : impl_(std::move(other.impl_)) {}

Window& Window::operator=(Window&& other) noexcept {
  if (this != &other) {
    if (impl_ && impl_->window) {
      glfwDestroyWindow(impl_->window);
      if (g_window_count.fetch_sub(1) == 1) {
        glfwTerminate();
      }
    }
    impl_ = std::move(other.impl_);
  }
  return *this;
}

Window::~Window() {
  if (impl_ && impl_->window) {
    glfwDestroyWindow(impl_->window);
    if (g_window_count.fetch_sub(1) == 1) {
      glfwTerminate();
    }
  }
}

bool Window::should_close() const { return glfwWindowShouldClose(impl_->window); }

void Window::request_close() { glfwSetWindowShouldClose(impl_->window, GLFW_TRUE); }

void Window::poll_events() { glfwPollEvents(); }

void Window::swap_buffers() { glfwSwapBuffers(impl_->window); }

void Window::framebuffer_size(int& width, int& height) const {
  glfwGetFramebufferSize(impl_->window, &width, &height);
}

void* Window::native_handle() const noexcept { return impl_->window; }

bool load_gl_functions() {
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
    ARCANE_LOG_ERROR(kLogCategory, "gladLoadGL failed");
    return false;
  }
  if (GLAD_GL_KHR_debug) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_callback, nullptr);
  }
  return true;
}

}  // namespace arcane
