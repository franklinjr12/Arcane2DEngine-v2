// Requires a real (even if offscreen) OpenGL context; CTest labels this
// "gl" and the test itself exits 77 (CTest's SKIP_RETURN_CODE) when no
// display/driver is available, so headless CI without Xvfb still passes.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "arcane/hud.hpp"
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

  // Exercise native filesystem paths, including Windows paths outside the
  // current ANSI code page. Close the file before loading and removing it.
  const auto image_path = std::filesystem::temp_directory_path() / std::filesystem::path(u8"arcane_\u6d4b\u8bd5_texture.ppm");
  {
    std::ofstream image(image_path, std::ios::binary);
    image << "P6\n1 1\n255\n";
    const char pixel[] = {static_cast<char>(255), 0, 0};
    image.write(pixel, sizeof(pixel));
    ARCANE_ASSERT_TRUE(image.good());
  }
  auto file_texture = arcane::Texture::load_from_file(image_path);
  std::filesystem::remove(image_path);
  ARCANE_ASSERT_TRUE(file_texture.has_value());
  ARCANE_ASSERT_TRUE(file_texture->width() == 1 && file_texture->height() == 1);

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

  // HUD pass after the world: a full-screen half-filled bar (screen-space,
  // 720 layout units == whole screen height) over the red world.
  auto hud_renderer = arcane::HudRenderer::create();
  ARCANE_ASSERT_TRUE(hud_renderer.has_value());
  arcane::Hud hud;
  hud.add(arcane::HudProgressBar{.width = 720.0f * static_cast<float>(fb_width) / static_cast<float>(fb_height),
                                 .height = 720.0f,
                                 .fraction = 0.5f,
                                 .fill = {0.0f, 1.0f, 0.0f, 1.0f},
                                 .background = {0.0f, 0.0f, 1.0f, 1.0f}});
  renderer->set_camera(0.0f, 0.0f, 1.0f);
  renderer->begin_frame(fb_width, fb_height, arcane::Color{0.0f, 0.0f, 0.0f, 1.0f});
  renderer->draw(arcane::SpriteDraw{.texture = &*texture,
                                     .x = 0.0f,
                                     .y = 0.0f,
                                     .width = static_cast<float>(fb_width),
                                     .height = static_cast<float>(fb_height)});
  hud_renderer->draw(hud, *renderer, fb_width, fb_height);
  renderer->end_frame();
  const arcane::Color filled = renderer->read_pixel(fb_width / 4, fb_height / 2);
  const arcane::Color empty = renderer->read_pixel(fb_width * 3 / 4, fb_height / 2);
  ARCANE_ASSERT_TRUE(filled.g > 0.9f && filled.r < 0.1f);
  ARCANE_ASSERT_TRUE(empty.b > 0.9f && empty.g < 0.1f);

  // Text: glyph pixels must put at least one white pixel on screen.
  hud.clear();
  hud.add(arcane::HudText{.text = "HHH", .anchor = arcane::HudAnchor::Center, .font_scale = 20.0f});
  renderer->begin_frame(fb_width, fb_height, arcane::Color{0.0f, 0.0f, 0.0f, 1.0f});
  hud_renderer->draw(hud, *renderer, fb_width, fb_height);
  renderer->end_frame();
  bool text_pixel_found = false;
  for (int y = 0; y < fb_height && !text_pixel_found; ++y) {
    for (int x = 0; x < fb_width; ++x) {
      if (renderer->read_pixel(x, y).r > 0.9f) {
        text_pixel_found = true;
        break;
      }
    }
  }
  ARCANE_ASSERT_TRUE(text_pixel_found);

  // UTF-8 text: accented and unsupported characters must draw without trouble.
  hud.clear();
  hud.add(arcane::HudText{.text = "A\xC3\xA7\xC3\xA3o \xE4\xB8\xAD \xFF", .anchor = arcane::HudAnchor::Center,
                          .font_scale = 12.0f});
  renderer->begin_frame(fb_width, fb_height, arcane::Color{0.0f, 0.0f, 0.0f, 1.0f});
  hud_renderer->draw(hud, *renderer, fb_width, fb_height);
  renderer->end_frame();
  int utf8_pixels = 0;
  for (int y = 0; y < fb_height; ++y) {
    for (int x = 0; x < fb_width; ++x) {
      if (renderer->read_pixel(x, y).r > 0.5f) ++utf8_pixels;
    }
  }
  ARCANE_ASSERT_TRUE(utf8_pixels > 50);

  return 0;
}
