#pragma once

#include <expected>
#include <memory>
#include <string>

#include "arcane/export.hpp"

namespace arcane {

struct WindowConfig {
  int width = 1280;
  int height = 720;
  std::string title = "Arcane2D";
  bool vsync = true;
  // Hidden/offscreen window: still creates a real GL context (so rendering
  // tests work), but never shows an on-screen surface. Used by headless
  // tests and the player's --hidden flag.
  bool hidden = false;
};

// Owns a GLFW window + OpenGL 3.3 core context. GLFW types never appear in
// this public header.
class ARCANE_API Window {
 public:
  static std::expected<Window, std::string> create(const WindowConfig& config);

  Window(Window&& other) noexcept;
  Window& operator=(Window&& other) noexcept;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window();

  bool should_close() const;
  void request_close();
  void poll_events();
  void swap_buffers();
  void framebuffer_size(int& width, int& height) const;

  // Opaque GLFWwindow*, exposed only for other engine subsystems (e.g. the
  // renderer's ImGui backend later) that must already link GLFW directly.
  void* native_handle() const noexcept;

 private:
  Window();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Loads OpenGL 3.3 core function pointers against the current context.
// Must be called once, after a Window is created and made current.
ARCANE_API bool load_gl_functions();

}  // namespace arcane
