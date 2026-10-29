#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "arcane/export.hpp"

namespace arcane {

struct Color {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
  friend bool operator==(const Color&, const Color&) = default;
};

// Normalized UV sub-rectangle within a texture: (u0, v0, u1, v1).
struct UvRect {
  float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

enum class TextureFilter { Nearest, Linear };

// GPU texture handle. Owns the underlying OpenGL texture object.
class ARCANE_API Texture {
 public:
  static std::expected<Texture, std::string> load_from_file(const std::filesystem::path& path,
                                                              TextureFilter filter = TextureFilter::Linear);
  static std::expected<Texture, std::string> load_from_memory(std::span<const unsigned char> encoded_bytes,
                                                                TextureFilter filter = TextureFilter::Linear);
  // Builds a texture directly from raw RGBA8 pixels (width*height*4 bytes).
  static std::expected<Texture, std::string> load_from_pixels(std::span<const unsigned char> rgba_pixels,
                                                                int width, int height,
                                                                TextureFilter filter = TextureFilter::Linear);

  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  ~Texture();

  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }
  std::uint32_t native_id() const noexcept { return id_; }

 private:
  Texture() = default;
  std::uint32_t id_ = 0;
  int width_ = 0;
  int height_ = 0;
};

struct SpriteDraw {
  const Texture* texture = nullptr;
  float x = 0.0f, y = 0.0f;
  float width = 0.0f, height = 0.0f;
  float rotation_radians = 0.0f;
  // Origin as a fraction of (width, height); (0.5, 0.5) rotates/scales around the center.
  float origin_x = 0.5f, origin_y = 0.5f;
  Color tint{};
  UvRect uv{};
};

// Small batched 2D sprite renderer: shader-based textured quads with an
// orthographic camera. Draw calls are batched per contiguous run of the
// same texture and flushed on texture change or end_frame().
class ARCANE_API SpriteRenderer {
 public:
  static std::expected<SpriteRenderer, std::string> create();

  SpriteRenderer(SpriteRenderer&& other) noexcept;
  SpriteRenderer& operator=(SpriteRenderer&& other) noexcept;
  SpriteRenderer(const SpriteRenderer&) = delete;
  SpriteRenderer& operator=(const SpriteRenderer&) = delete;
  ~SpriteRenderer();

  // Sets the orthographic camera: world-space (x, y) is centered in the
  // viewport, visible world width is viewport_width / zoom.
  void set_camera(float x, float y, float zoom = 1.0f);

  void begin_frame(int framebuffer_width, int framebuffer_height, const Color& clear_color);
  void draw(const SpriteDraw& sprite);
  // Draws everything batched so far (using the current camera). Lets callers
  // change the camera mid-frame, e.g. world pass then screen-space HUD pass.
  void flush();
  void end_frame();

  // Test helper: reads back a single pixel from the currently bound
  // framebuffer as RGBA in [0, 1]. Only meaningful right after end_frame().
  Color read_pixel(int x, int y) const;

 private:
  SpriteRenderer();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace arcane
