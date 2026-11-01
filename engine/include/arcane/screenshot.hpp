#pragma once

#include <expected>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

#include "arcane/export.hpp"

namespace arcane {

// Rendering evidence (docs/formats/replay.md): read the rendered frame back and compare it with a golden PNG.
// Rows are stored top to bottom, 4 bytes (RGBA8) per pixel.
struct ImageRgba {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

// Reads the currently bound framebuffer (the window's back buffer after SpriteRenderer::end_frame()). Needs a
// current GL context with the functions loaded; fails otherwise or for a non-positive size.
ARCANE_API std::expected<ImageRgba, std::string> capture_framebuffer(int width, int height);

// Runs `draw` with an offscreen framebuffer object (RGBA8, width x height, 1..8192 per side) bound and returns what it
// drew. Needs a current GL context; the previous framebuffer binding is restored.
ARCANE_API std::expected<ImageRgba, std::string> render_offscreen(int width, int height, const std::function<void()>& draw);

// PNG via the pinned stb_image_write. The file is written to a temp name, closed, then renamed over `path`.
ARCANE_API std::expected<void, std::string> write_png(const std::filesystem::path& path, const ImageRgba& image);
ARCANE_API std::expected<ImageRgba, std::string> read_png(const std::filesystem::path& path);

struct ImageDiff {
  bool same_size = false;
  int max_channel_delta = 0;     // largest |a - b| over all channels (0 when sizes differ)
  long differing_pixels = 0;      // pixels with any channel delta above the tolerance
};

// Pixels differ when any RGBA channel differs by more than `channel_tolerance` (0..255).
ARCANE_API ImageDiff compare_images(const ImageRgba& a, const ImageRgba& b, int channel_tolerance);

// Pixel rectangle (top-left origin) excluded from a comparison, e.g. a clock or random particles.
struct ImageMaskRect {
  int x = 0, y = 0, width = 0, height = 0;
};
ARCANE_API ImageDiff compare_images(const ImageRgba& a, const ImageRgba& b, int channel_tolerance,
                                    const std::vector<ImageMaskRect>& masks);

}  // namespace arcane
