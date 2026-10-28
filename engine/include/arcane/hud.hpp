#pragma once

#include <string>
#include <variant>
#include <vector>

#include "arcane/export.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

// Minimal in-game UI (doc section 7.7): screen-space text, images and
// progress bars, positioned by anchor + offset and scaled with the screen
// height. Owned by the engine runtime, no Dear ImGui involved, so exported
// games get a HUD without linking the editor UI.

enum class HudAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

struct HudRect {
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;  // pixels, origin top-left
};

// Layout units are pixels at `hud_reference_height`; everything scales by
// screen_height / reference so a HUD authored for 720p fills the same
// fraction of the screen at any resolution.
inline constexpr float hud_reference_height = 720.0f;
ARCANE_API float hud_scale(int screen_height) noexcept;

// Places a w*h element: its anchor-matching point (e.g. the bottom-right
// corner for BottomRight) lands on the same point of the screen, then
// offset is added. w, h, offset are in layout units; result is in pixels.
ARCANE_API HudRect hud_layout(HudAnchor anchor, float offset_x, float offset_y, float w, float h, int screen_width,
                              int screen_height) noexcept;

// Size of `text` in layout units when drawn at `font_scale` (1 = the
// built-in 7px-tall bitmap font; ASCII only).
ARCANE_API void hud_text_size(const std::string& text, float font_scale, float& out_w, float& out_h) noexcept;

struct HudText {
  std::string text;
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float font_scale = 2.0f;
  Color color{};
};

struct HudImage {
  const Texture* texture = nullptr;
  UvRect uv{};
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float width = 32.0f, height = 32.0f;
  Color tint{};
};

struct HudProgressBar {
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float width = 200.0f, height = 16.0f;
  float fraction = 1.0f;  // clamped to [0, 1]
  Color fill{0.2f, 0.8f, 0.3f, 1.0f};
  Color background{0.0f, 0.0f, 0.0f, 0.6f};
};

using HudElement = std::variant<HudText, HudImage, HudProgressBar>;

// Retained list of HUD elements, drawn in insertion order.
class ARCANE_API Hud {
 public:
  void add(HudElement element) { elements_.push_back(std::move(element)); }
  void clear() noexcept { elements_.clear(); }
  const std::vector<HudElement>& elements() const noexcept { return elements_; }

 private:
  std::vector<HudElement> elements_;
};

// The Hud live for the current Play/runtime session, set by the player/editor
// main loop so gameplay code can add/update elements from on_update (mirrors
// active_input_map / active_audio_system). Null outside Play.
ARCANE_API void set_active_hud(Hud* hud) noexcept;
ARCANE_API Hud* active_hud() noexcept;

// Draws a Hud through a SpriteRenderer in screen space. Needs a GL context
// (owns a 1x1 white texture used for bars and text quads).
class ARCANE_API HudRenderer {
 public:
  static std::expected<HudRenderer, std::string> create();

  // Call between SpriteRenderer::begin_frame and end_frame, after the world
  // has been drawn. Flushes the world batch and switches the renderer's
  // camera to pixel coordinates (origin top-left).
  void draw(const Hud& hud, SpriteRenderer& renderer, int screen_width, int screen_height);

 private:
  explicit HudRenderer(Texture white) : white_(std::move(white)) {}
  Texture white_;
};

}  // namespace arcane
