// Requires a real (even if offscreen) OpenGL context; CTest labels this
// "gl" and the test itself exits 77 (CTest's SKIP_RETURN_CODE) when no
// display/driver is available, so headless CI without Xvfb still passes.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "arcane/renderer.hpp"
#include "arcane/window.hpp"
#include "test_assert.hpp"

namespace {
constexpr int kSkipReturnCode = 77;
}

int main() {
  auto window = arcane::Window::create({.width = 64, .height = 64, .title = "test_renderer_gl", .hidden = true});
  if (!window) {
    std::fprintf(stderr, "no GL-capable display available, skipping: %s\n", window.error().c_str());
    return kSkipReturnCode;
  }

  if (!arcane::load_gl_functions()) {
    std::fprintf(stderr, "failed to load GL functions, skipping\n");
    return kSkipReturnCode;
  }

  auto renderer = arcane::SpriteRenderer::create();
  ARCANE_ASSERT_TRUE(renderer.has_value());

  std::vector<unsigned char> red_pixels(64 * 64 * 4);
  for (size_t i = 0; i < red_pixels.size(); i += 4) {
    red_pixels[i + 0] = 255;
    red_pixels[i + 1] = 0;
    red_pixels[i + 2] = 0;
    red_pixels[i + 3] = 255;
  }
  auto texture = arcane::Texture::load_from_pixels(red_pixels, 64, 64);
  ARCANE_ASSERT_TRUE(texture.has_value());

  int fb_width = 0, fb_height = 0;
  window->framebuffer_size(fb_width, fb_height);
  ARCANE_ASSERT_TRUE(fb_width > 0 && fb_height > 0);

  renderer->set_camera(0.0f, 0.0f, 1.0f);
  renderer->begin_frame(fb_width, fb_height, arcane::Color{0.0f, 0.0f, 0.0f, 1.0f});
  renderer->draw(arcane::SpriteDraw{.texture = &*texture,
                                     .x = 0.0f,
                                     .y = 0.0f,
                                     .width = static_cast<float>(fb_width),
                                     .height = static_cast<float>(fb_height)});
  renderer->end_frame();

  const arcane::Color center = renderer->read_pixel(fb_width / 2, fb_height / 2);
  ARCANE_ASSERT_TRUE(center.r > 0.9f);
  ARCANE_ASSERT_TRUE(center.g < 0.1f);
  ARCANE_ASSERT_TRUE(center.b < 0.1f);

  return 0;
}
