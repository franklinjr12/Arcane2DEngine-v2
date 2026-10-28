#include "arcane/hud.hpp"

#include <algorithm>
#include <array>

#include <stb_easy_font.h>

namespace arcane {

namespace {

// Where on an element/screen each anchor sits, as fractions of width/height.
void anchor_fractions(HudAnchor anchor, float& fx, float& fy) noexcept {
  const int index = static_cast<int>(anchor);
  fx = static_cast<float>(index % 3) * 0.5f;
  fy = static_cast<float>(index / 3) * 0.5f;
}

void draw_quad(SpriteRenderer& renderer, const Texture* white, float x, float y, float w, float h,
               const Color& color) {
  renderer.draw(SpriteDraw{.texture = white, .x = x, .y = y, .width = w, .height = h,
                           .origin_x = 0.0f, .origin_y = 0.0f, .tint = color});
}

}  // namespace

namespace {
Hud* g_active_hud = nullptr;
}

void set_active_hud(Hud* hud) noexcept { g_active_hud = hud; }
Hud* active_hud() noexcept { return g_active_hud; }

float hud_scale(int screen_height) noexcept { return static_cast<float>(screen_height) / hud_reference_height; }

HudRect hud_layout(HudAnchor anchor, float offset_x, float offset_y, float w, float h, int screen_width,
                   int screen_height) noexcept {
  const float scale = hud_scale(screen_height);
  float fx = 0.0f, fy = 0.0f;
  anchor_fractions(anchor, fx, fy);
  HudRect rect;
  rect.w = w * scale;
  rect.h = h * scale;
  rect.x = fx * (static_cast<float>(screen_width) - rect.w) + offset_x * scale;
  rect.y = fy * (static_cast<float>(screen_height) - rect.h) + offset_y * scale;
  return rect;
}

void hud_text_size(const std::string& text, float font_scale, float& out_w, float& out_h) noexcept {
  out_w = static_cast<float>(stb_easy_font_width(const_cast<char*>(text.c_str()))) * font_scale;
  out_h = static_cast<float>(stb_easy_font_height(const_cast<char*>(text.c_str()))) * font_scale;
}

std::expected<HudRenderer, std::string> HudRenderer::create() {
  const std::array<unsigned char, 4> white_pixel = {255, 255, 255, 255};
  auto texture = Texture::load_from_pixels(white_pixel, 1, 1, TextureFilter::Nearest);
  if (!texture) return std::unexpected(texture.error());
  return HudRenderer(std::move(*texture));
}

void HudRenderer::draw(const Hud& hud, SpriteRenderer& renderer, int screen_width, int screen_height) {
  renderer.flush();
  renderer.set_camera(static_cast<float>(screen_width) * 0.5f, static_cast<float>(screen_height) * 0.5f, 1.0f);
  const float scale = hud_scale(screen_height);

  for (const HudElement& element : hud.elements()) {
    if (const auto* image = std::get_if<HudImage>(&element)) {
      if (!image->texture) continue;
      const HudRect r = hud_layout(image->anchor, image->offset_x, image->offset_y, image->width, image->height,
                                   screen_width, screen_height);
      renderer.draw(SpriteDraw{.texture = image->texture, .x = r.x, .y = r.y, .width = r.w, .height = r.h,
                               .origin_x = 0.0f, .origin_y = 0.0f, .tint = image->tint, .uv = image->uv});
    } else if (const auto* bar = std::get_if<HudProgressBar>(&element)) {
      const HudRect r = hud_layout(bar->anchor, bar->offset_x, bar->offset_y, bar->width, bar->height,
                                   screen_width, screen_height);
      draw_quad(renderer, &white_, r.x, r.y, r.w, r.h, bar->background);
      const float fraction = std::clamp(bar->fraction, 0.0f, 1.0f);
      if (fraction > 0.0f) draw_quad(renderer, &white_, r.x, r.y, r.w * fraction, r.h, bar->fill);
    } else if (const auto* text = std::get_if<HudText>(&element)) {
      float w = 0.0f, h = 0.0f;
      hud_text_size(text->text, text->font_scale, w, h);
      const HudRect r = hud_layout(text->anchor, text->offset_x, text->offset_y, w, h, screen_width, screen_height);
      // stb_easy_font emits 4 vertices (16 bytes each) per glyph stroke quad.
      static thread_local std::array<char, 99999> buffer;
      const int quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text->text.c_str()), nullptr,
                                            buffer.data(), static_cast<int>(buffer.size()));
      const float k = text->font_scale * scale;
      for (int q = 0; q < quads; ++q) {
        float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
        for (int v = 0; v < 4; ++v) {
          const float* position = reinterpret_cast<const float*>(buffer.data() + (q * 4 + v) * 16);
          min_x = std::min(min_x, position[0]);
          max_x = std::max(max_x, position[0]);
          min_y = std::min(min_y, position[1]);
          max_y = std::max(max_y, position[1]);
        }
        draw_quad(renderer, &white_, r.x + min_x * k, r.y + min_y * k, (max_x - min_x) * k, (max_y - min_y) * k,
                  text->color);
      }
    }
  }
}

}  // namespace arcane
