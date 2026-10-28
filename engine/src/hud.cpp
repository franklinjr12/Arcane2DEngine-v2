#include "arcane/hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <json.hpp>

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

// Draws `text` with its top-left at (x, y) pixels.
void draw_text(SpriteRenderer& renderer, const Texture* white, const std::string& text, float x, float y, float k,
               const Color& color) {
  // stb_easy_font emits 4 vertices (16 bytes each) per glyph stroke quad.
  static thread_local std::array<char, 99999> buffer;
  const int quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text.c_str()), nullptr, buffer.data(),
                                        static_cast<int>(buffer.size()));
  for (int q = 0; q < quads; ++q) {
    float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
    for (int v = 0; v < 4; ++v) {
      const float* position = reinterpret_cast<const float*>(buffer.data() + (q * 4 + v) * 16);
      min_x = std::min(min_x, position[0]);
      max_x = std::max(max_x, position[0]);
      min_y = std::min(min_y, position[1]);
      max_y = std::max(max_y, position[1]);
    }
    draw_quad(renderer, white, x + min_x * k, y + min_y * k, (max_x - min_x) * k, (max_y - min_y) * k, color);
  }
}

const Color& button_color(const HudButton& b) {
  switch (b.state) {
    case HudButtonState::Hover: return b.hover;
    case HudButtonState::Pressed: return b.pressed;
    case HudButtonState::Disabled: return b.disabled;
    case HudButtonState::Normal: break;
  }
  return b.normal;
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

bool Hud::clicked(std::string_view id) const {
  return std::find(clicks_.begin(), clicks_.end(), id) != clicks_.end();
}

// ---- JSON ---------------------------------------------------------------

namespace {
constexpr int kHudFormatVersion = 1;
constexpr std::array<const char*, 9> kAnchorNames = {"top_left", "top", "top_right", "left", "center",
                                                     "right", "bottom_left", "bottom", "bottom_right"};

nlohmann::json color_to_json(const Color& c) { return nlohmann::json::array({c.r, c.g, c.b, c.a}); }

Color color_from_json(const nlohmann::json& j, const std::string& key, Color fallback) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() != 4) return fallback;
  return Color{j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>()};
}

std::expected<HudAnchor, std::string> anchor_from_json(const nlohmann::json& j, HudAnchor fallback) {
  if (!j.contains("anchor")) return fallback;
  const std::string name = j["anchor"].is_string() ? j["anchor"].get<std::string>() : std::string();
  for (size_t i = 0; i < kAnchorNames.size(); ++i) {
    if (name == kAnchorNames[i]) return static_cast<HudAnchor>(i);
  }
  return std::unexpected(std::format("unknown HUD anchor '{}'", name));
}

void put_layout(nlohmann::json& j, HudAnchor anchor, float ox, float oy) {
  j["anchor"] = kAnchorNames[static_cast<size_t>(anchor)];
  j["offset"] = nlohmann::json::array({ox, oy});
}

void get_offset(const nlohmann::json& j, float& ox, float& oy) {
  if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() == 2) {
    ox = j["offset"][0].get<float>();
    oy = j["offset"][1].get<float>();
  }
}
}  // namespace

std::string Hud::save_to_json() const {
  nlohmann::json root;
  root["format_version"] = kHudFormatVersion;
  nlohmann::json list = nlohmann::json::array();
  for (const HudElement& element : elements_) {
    nlohmann::json j;
    if (const auto* t = std::get_if<HudText>(&element)) {
      j["type"] = "text";
      j["text"] = t->text;
      put_layout(j, t->anchor, t->offset_x, t->offset_y);
      j["font_scale"] = t->font_scale;
      j["color"] = color_to_json(t->color);
    } else if (const auto* bar = std::get_if<HudProgressBar>(&element)) {
      j["type"] = "progress_bar";
      put_layout(j, bar->anchor, bar->offset_x, bar->offset_y);
      j["size"] = nlohmann::json::array({bar->width, bar->height});
      j["fraction"] = bar->fraction;
      j["fill"] = color_to_json(bar->fill);
      j["background"] = color_to_json(bar->background);
    } else if (const auto* b = std::get_if<HudButton>(&element)) {
      j["type"] = "button";
      j["id"] = b->id;
      j["label"] = b->label;
      put_layout(j, b->anchor, b->offset_x, b->offset_y);
      j["size"] = nlohmann::json::array({b->width, b->height});
      j["font_scale"] = b->font_scale;
      j["enabled"] = b->enabled;
      j["normal"] = color_to_json(b->normal);
      j["hover"] = color_to_json(b->hover);
      j["pressed"] = color_to_json(b->pressed);
      j["disabled"] = color_to_json(b->disabled);
      j["text_color"] = color_to_json(b->text_color);
    } else {
      continue;  // images are runtime-only
    }
    list.push_back(std::move(j));
  }
  root["elements"] = std::move(list);
  return root.dump(2);
}

std::expected<Hud, std::string> Hud::load_from_json(const std::string& text) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
    if (root.value("format_version", 0) != kHudFormatVersion) {
      return std::unexpected(std::format("unsupported HUD format_version {}", root.value("format_version", 0)));
    }
    Hud hud;
    for (const auto& j : root.value("elements", nlohmann::json::array())) {
      const std::string type = j.value("type", std::string());
      auto anchor = anchor_from_json(j, HudAnchor::TopLeft);
      if (!anchor) return std::unexpected(anchor.error());
      auto size = [&](float& w, float& h) {
        if (j.contains("size") && j["size"].is_array() && j["size"].size() == 2) {
          w = j["size"][0].get<float>();
          h = j["size"][1].get<float>();
        }
      };
      if (type == "text") {
        HudText t;
        t.text = j.value("text", std::string());
        t.anchor = *anchor;
        get_offset(j, t.offset_x, t.offset_y);
        t.font_scale = j.value("font_scale", t.font_scale);
        t.color = color_from_json(j, "color", t.color);
        hud.add(std::move(t));
      } else if (type == "progress_bar") {
        HudProgressBar bar;
        bar.anchor = *anchor;
        get_offset(j, bar.offset_x, bar.offset_y);
        size(bar.width, bar.height);
        bar.fraction = j.value("fraction", bar.fraction);
        bar.fill = color_from_json(j, "fill", bar.fill);
        bar.background = color_from_json(j, "background", bar.background);
        hud.add(std::move(bar));
      } else if (type == "button") {
        HudButton b;
        b.id = j.value("id", std::string());
        b.label = j.value("label", std::string());
        b.anchor = j.contains("anchor") ? *anchor : HudAnchor::Center;
        get_offset(j, b.offset_x, b.offset_y);
        size(b.width, b.height);
        b.font_scale = j.value("font_scale", b.font_scale);
        b.enabled = j.value("enabled", true);
        b.normal = color_from_json(j, "normal", b.normal);
        b.hover = color_from_json(j, "hover", b.hover);
        b.pressed = color_from_json(j, "pressed", b.pressed);
        b.disabled = color_from_json(j, "disabled", b.disabled);
        b.text_color = color_from_json(j, "text_color", b.text_color);
        hud.add(std::move(b));
      } else {
        return std::unexpected(std::format("unknown HUD element type '{}'", type));
      }
    }
    return hud;
  } catch (const nlohmann::json::exception& ex) {
    return std::unexpected(std::format("malformed HUD JSON: {}", ex.what()));
  }
}

// ---- input routing ------------------------------------------------------

namespace {
std::uint64_t pack_binding(const InputBinding& b) {
  return (static_cast<std::uint64_t>(static_cast<int>(b.device)) << 32) | static_cast<std::uint32_t>(b.code);
}
}  // namespace

void MaskedInputBackend::mask(InputDevice device, int code) { masked_.insert(pack_binding({device, code})); }

bool MaskedInputBackend::is_down(InputDevice device, int code) const {
  if (!inner_ || masked_.contains(pack_binding({device, code}))) return false;
  return inner_->is_down(device, code);
}

bool HudInput::any_down(const InputBackend& backend, const std::vector<InputBinding>& bindings) {
  return std::any_of(bindings.begin(), bindings.end(),
                     [&](const InputBinding& b) { return backend.is_down(b.device, b.code); });
}

void HudInput::mask_all(const std::vector<InputBinding>& bindings) {
  for (const InputBinding& b : bindings) masked_.mask(b.device, b.code);
}

void HudInput::update(Hud& hud, const InputBackend& backend, int screen_width, int screen_height, Vec2 origin) {
  hud.clicks_.clear();
  masked_ = MaskedInputBackend(backend);
  pointer_consumed_ = false;
  auto& elements = hud.elements_;
  const int count = static_cast<int>(elements.size());

  auto button_at = [&](int i) -> HudButton* { return i >= 0 && i < count ? std::get_if<HudButton>(&elements[i]) : nullptr; };

  // Hit-test (topmost = last drawn wins).
  const Vec2 mouse = backend.mouse_position();
  const float px = mouse.x - origin.x, py = mouse.y - origin.y;
  int hovered = -1;
  for (int i = 0; i < count; ++i) {
    const HudButton* b = button_at(i);
    if (!b) continue;
    const HudRect r = hud_layout(b->anchor, b->offset_x, b->offset_y, b->width, b->height, screen_width, screen_height);
    if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) hovered = i;
  }

  // Pointer.
  const bool pointer_down = backend.is_down(InputDevice::MouseButton, pointer_button);
  std::vector<int> clicked;
  if (pointer_down && !pointer_was_down_) pressed_button_ = hovered;  // -1 if the press began off-HUD
  if (!button_at(pressed_button_)) pressed_button_ = -1;
  if (pressed_button_ >= 0 && pointer_down) {
    masked_.mask(InputDevice::MouseButton, pointer_button);
    pointer_consumed_ = true;
  }
  if (!pointer_down && pointer_was_down_) {
    const HudButton* b = button_at(pressed_button_);
    if (b && b->enabled && pressed_button_ == hovered) clicked.push_back(pressed_button_);
    pressed_button_ = -1;
  }
  pointer_was_down_ = pointer_down;

  // Focus: hover grabs it; leaving a pointer-grabbed focus releases it.
  if (!button_at(focus_)) focus_ = -1;
  if (hovered >= 0 && hovered != last_hovered_) {  // only on change, so a resting pointer doesn't fight the keyboard
    focus_ = hovered;
    focus_from_pointer_ = true;
  } else if (hovered < 0 && focus_from_pointer_) {
    focus_ = -1;
  }
  last_hovered_ = hovered;

  // Keyboard / gamepad navigation among enabled buttons (wraps).
  const bool next_down = any_down(backend, next_bindings);
  const bool previous_down = any_down(backend, previous_bindings);
  const bool confirm_down = any_down(backend, confirm_bindings);
  auto step = [&](int direction) {
    int i = focus_;
    for (int n = 0; n < count; ++n) {
      i = i < 0 ? (direction > 0 ? 0 : count - 1) : (i + direction + count) % count;
      const HudButton* b = button_at(i);
      if (b && b->enabled) {
        focus_ = i;
        focus_from_pointer_ = false;
        return;
      }
    }
  };
  if (next_down && !next_was_down_) step(+1);
  if (previous_down && !previous_was_down_) step(-1);
  if (!confirm_down && confirm_was_down_) {
    const HudButton* b = button_at(focus_);
    if (b && b->enabled && !focus_from_pointer_) clicked.push_back(focus_);
  }
  // Pointer-focus confirm is not a click; keyboard focus only.
  const bool keyboard_focus = focus_ >= 0 && !focus_from_pointer_;
  if (keyboard_focus) {
    mask_all(next_bindings);
    mask_all(previous_bindings);
    mask_all(confirm_bindings);
  }
  next_was_down_ = next_down;
  previous_was_down_ = previous_down;
  confirm_was_down_ = confirm_down;

  // Visual state.
  for (int i = 0; i < count; ++i) {
    HudButton* b = button_at(i);
    if (!b) continue;
    b->focused = i == focus_;
    if (!b->enabled) {
      b->state = HudButtonState::Disabled;
    } else if ((pointer_down && pressed_button_ == i && hovered == i) ||
               (confirm_down && keyboard_focus && focus_ == i)) {
      b->state = HudButtonState::Pressed;
    } else if (hovered == i || focus_ == i) {
      b->state = HudButtonState::Hover;
    } else {
      b->state = HudButtonState::Normal;
    }
  }

  // Events last: handlers may edit the HUD.
  std::vector<std::string> ids;
  for (int i : clicked) ids.push_back(std::get<HudButton>(elements[i]).id);
  hud.clicks_ = ids;
  if (hud.click_handler_) {
    for (const std::string& id : ids) hud.click_handler_(id);
  }
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
      draw_text(renderer, &white_, text->text, r.x, r.y, text->font_scale * scale, text->color);
    } else if (const auto* button = std::get_if<HudButton>(&element)) {
      const HudRect r = hud_layout(button->anchor, button->offset_x, button->offset_y, button->width, button->height,
                                   screen_width, screen_height);
      draw_quad(renderer, &white_, r.x, r.y, r.w, r.h, button_color(*button));
      if (button->focused) {
        const float t = std::max(1.0f, 2.0f * scale);
        const Color outline{1.0f, 0.9f, 0.3f, 1.0f};
        draw_quad(renderer, &white_, r.x, r.y, r.w, t, outline);
        draw_quad(renderer, &white_, r.x, r.y + r.h - t, r.w, t, outline);
        draw_quad(renderer, &white_, r.x, r.y, t, r.h, outline);
        draw_quad(renderer, &white_, r.x + r.w - t, r.y, t, r.h, outline);
      }
      float tw = 0.0f, th = 0.0f;
      hud_text_size(button->label, button->font_scale, tw, th);
      Color label_color = button->text_color;
      if (button->state == HudButtonState::Disabled) label_color.a *= 0.5f;
      draw_text(renderer, &white_, button->label, r.x + (r.w - tw * scale) * 0.5f, r.y + (r.h - th * scale) * 0.5f,
                button->font_scale * scale, label_color);
    }
  }
}

}  // namespace arcane
