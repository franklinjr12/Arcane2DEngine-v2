#include "arcane/screenshot.hpp"

#include <glad/gl.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

namespace arcane {

namespace {

void append_png_bytes(void* context, void* data, int size) {
  static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

}  // namespace

std::expected<ImageRgba, std::string> capture_framebuffer(int width, int height) {
  if (width <= 0 || height <= 0) return std::unexpected(std::string("screenshot: size must be positive"));
  ImageRgba image;
  image.width = width;
  image.height = height;
  image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glFinish();
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
  if (glGetError() != GL_NO_ERROR) return std::unexpected(std::string("screenshot: glReadPixels failed"));
  // GL rows run bottom to top; flip so the image reads like a screen.
  const std::size_t stride = static_cast<std::size_t>(width) * 4;
  for (int row = 0; row < height / 2; ++row) {
    std::swap_ranges(image.pixels.begin() + static_cast<std::ptrdiff_t>(row * stride),
                     image.pixels.begin() + static_cast<std::ptrdiff_t>((row + 1) * stride),
                     image.pixels.begin() + static_cast<std::ptrdiff_t>((height - 1 - row) * stride));
  }
  return image;
}

std::expected<void, std::string> write_png(const std::filesystem::path& path, const ImageRgba& image) {
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::unexpected(std::string("png: image size does not match its pixel buffer"));
  }
  std::string png;
  if (!stbi_write_png_to_func(append_png_bytes, &png, image.width, image.height, 4, image.pixels.data(),
                              image.width * 4)) {
    return std::unexpected(std::string("png: encoding failed"));
  }
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path tmp = path.native() + std::filesystem::path(".arcane-tmp").native();
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    out.close();
    if (!out) return std::unexpected(std::format("png: cannot write {}", path.string()));
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) return std::unexpected(std::format("png: cannot write {}: {}", path.string(), ec.message()));
  return {};
}

std::expected<ImageRgba, std::string> read_png(const std::filesystem::path& path) {
  // Open the native path ourselves: stb's narrow filename API cannot open arbitrary Unicode names on Windows.
#ifdef _WIN32
  FILE* file = _wfopen(path.c_str(), L"rb");
#else
  FILE* file = std::fopen(path.c_str(), "rb");
#endif
  if (!file) return std::unexpected(std::format("png: cannot open {}", path.string()));
  int width = 0, height = 0, channels = 0;
  stbi_uc* data = stbi_load_from_file(file, &width, &height, &channels, 4);
  std::fclose(file);
  if (!data) return std::unexpected(std::format("png: decode failed: {}", stbi_failure_reason()));
  ImageRgba image;
  image.width = width;
  image.height = height;
  image.pixels.assign(data, data + static_cast<std::size_t>(width) * height * 4);
  stbi_image_free(data);
  return image;
}

std::expected<ImageRgba, std::string> render_offscreen(int width, int height, const std::function<void()>& draw) {
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
    return std::unexpected(std::string("render: size must be 1..8192 per side"));
  }
  GLint previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  GLuint fbo = 0, color = 0;
  glGenFramebuffers(1, &fbo);
  glGenRenderbuffers(1, &color);
  glBindRenderbuffer(GL_RENDERBUFFER, color);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
  std::expected<ImageRgba, std::string> result;
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    result = std::unexpected(std::string("render: offscreen framebuffer is incomplete"));
  } else {
    draw();
    result = capture_framebuffer(width, height);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
  glDeleteFramebuffers(1, &fbo);
  glDeleteRenderbuffers(1, &color);
  return result;
}

ImageDiff compare_images(const ImageRgba& a, const ImageRgba& b, int channel_tolerance) {
  return compare_images(a, b, channel_tolerance, {});
}

ImageDiff compare_images(const ImageRgba& a, const ImageRgba& b, int channel_tolerance,
                         const std::vector<ImageMaskRect>& masks) {
  ImageDiff diff;
  diff.same_size = a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size();
  if (!diff.same_size) return diff;
  for (std::size_t p = 0; p + 3 < a.pixels.size(); p += 4) {
    const int px = static_cast<int>((p / 4) % static_cast<std::size_t>(a.width));
    const int py = static_cast<int>((p / 4) / static_cast<std::size_t>(a.width));
    const bool masked = std::any_of(masks.begin(), masks.end(), [&](const ImageMaskRect& m) {
      return px >= m.x && px < m.x + m.width && py >= m.y && py < m.y + m.height;
    });
    if (masked) continue;
    int worst = 0;
    for (std::size_t c = 0; c < 4; ++c) worst = std::max(worst, std::abs(int(a.pixels[p + c]) - int(b.pixels[p + c])));
    diff.max_channel_delta = std::max(diff.max_channel_delta, worst);
    if (worst > channel_tolerance) ++diff.differing_pixels;
  }
  return diff;
}

}  // namespace arcane
