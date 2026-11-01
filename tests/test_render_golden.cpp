// AF12 rendering evidence: draws a fixed scene in a hidden 160x120 window, reads the frame back, round-trips it
// through PNG and compares it with tests/golden/render/basic.png. Set ARCANE_UPDATE_GOLDEN=1 to rewrite the golden.
// Comparison tolerance: each RGBA channel may differ by 2, and at most 0.5% of the pixels may differ by more
// (docs/formats/replay.md). Golden images are produced by Mesa llvmpipe under Xvfb; other drivers may need a
// regenerated golden. Exits 77 (CTest skip) without a GL display.
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "arcane/hud.hpp"
#include "arcane/renderer.hpp"
#include "arcane/screenshot.hpp"
#include "arcane/window.hpp"
#include "test_assert.hpp"

namespace {
constexpr int kSkipReturnCode = 77;
constexpr int kSize[2] = {160, 120};
constexpr int kChannelTolerance = 2;
constexpr double kMaxDifferingFraction = 0.005;
}  // namespace

int main() {
  auto window = arcane::Window::create({.width = kSize[0], .height = kSize[1], .title = "test_render_golden", .hidden = true});
  if (!window || !arcane::load_gl_functions()) {
    std::fprintf(stderr, "no GL-capable display available, skipping\n");
    return kSkipReturnCode;
  }
  auto renderer = arcane::SpriteRenderer::create();
  auto hud_renderer = arcane::HudRenderer::create();
  ARCANE_ASSERT_TRUE(renderer.has_value() && hud_renderer.has_value());

  std::vector<unsigned char> pixels(16 * 16 * 4);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      unsigned char* p = &pixels[(static_cast<std::size_t>(y) * 16 + x) * 4];
      p[0] = static_cast<unsigned char>(x * 16);
      p[1] = static_cast<unsigned char>(y * 16);
      p[2] = 128;
      p[3] = 255;
    }
  }
  auto texture = arcane::Texture::load_from_pixels(pixels, 16, 16, arcane::TextureFilter::Nearest);
  ARCANE_ASSERT_TRUE(texture.has_value());

  int width = 0, height = 0;
  window->framebuffer_size(width, height);
  arcane::Hud hud;
  hud.add(arcane::HudProgressBar{.width = 200.0f, .height = 40.0f, .fraction = 0.6f, .fill = {0.1f, 0.9f, 0.2f, 1.0f},
                                 .background = {0.2f, 0.2f, 0.3f, 1.0f}});
  renderer->set_camera(0.0f, 0.0f, 1.0f);
  renderer->begin_frame(width, height, arcane::Color{0.05f, 0.05f, 0.1f, 1.0f});
  renderer->draw(arcane::SpriteDraw{.texture = &*texture, .x = -30.0f, .y = 0.0f, .width = 48.0f, .height = 48.0f});
  renderer->draw(arcane::SpriteDraw{.texture = &*texture, .x = 30.0f, .y = 10.0f, .width = 32.0f, .height = 32.0f,
                                    .rotation_radians = 0.5f, .tint = {1.0f, 0.6f, 0.6f, 1.0f}});
  hud_renderer->draw(hud, *renderer, width, height);
  renderer->end_frame();

  auto image = arcane::capture_framebuffer(width, height);
  ARCANE_ASSERT_TRUE(image.has_value());
  const std::filesystem::path golden = std::filesystem::path(ARCANE_GOLDEN_DIR) / "render" / "basic.png";
  if (std::getenv("ARCANE_UPDATE_GOLDEN")) {
    ARCANE_ASSERT_TRUE(arcane::write_png(golden, *image).has_value());
    std::puts("golden updated");
    return 0;
  }
  // Round-trip through the PNG writer first, so encoding is covered too.
  const auto tmp = std::filesystem::temp_directory_path() / "arcane_render_golden_tmp.png";
  ARCANE_ASSERT_TRUE(arcane::write_png(tmp, *image).has_value());
  auto reread = arcane::read_png(tmp);
  std::filesystem::remove(tmp);
  ARCANE_ASSERT_TRUE(reread.has_value() && arcane::compare_images(*image, *reread, 0).differing_pixels == 0);

  auto expected = arcane::read_png(golden);
  if (!expected) {
    std::fprintf(stderr, "missing golden %s (run with ARCANE_UPDATE_GOLDEN=1)\n", golden.string().c_str());
    return 1;
  }
  const arcane::ImageDiff diff = arcane::compare_images(*image, *expected, kChannelTolerance);
  std::printf("size_match=%d max_delta=%d differing=%ld\n", diff.same_size, diff.max_channel_delta, diff.differing_pixels);
  ARCANE_ASSERT_TRUE(diff.same_size);
  ARCANE_ASSERT_TRUE(static_cast<double>(diff.differing_pixels) <= kMaxDifferingFraction * width * height);
  // The picture must not be blank: the sprite area differs from the clear color.
  const std::size_t center = (static_cast<std::size_t>(height / 2) * width + width / 2 - 30) * 4;
  ARCANE_ASSERT_TRUE(image->pixels[center] != 13 || image->pixels[center + 1] != 13);
  return 0;
}
