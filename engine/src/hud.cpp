#include "arcane/hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <json.hpp>
#include <unordered_set>

#include "canonical_json.hpp"

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

// Draws `text` with its top-left at (x, y) pixels; `pixel_size` is the em
// height in pixels. Glyphs missing from the font get a hollow box.
void draw_text(SpriteRenderer& renderer, const Texture* white, const Texture* atlas_texture, const FontAtlas& atlas,
               const std::string& text, float x, float y, float pixel_size, const Color& color) {
  const float k = pixel_size / atlas.pixel_size;
  const float line_height = font_line_height(pixel_size);
  float pen_x = x, line_top = y;
  for (const char32_t cp : decode_utf8(text)) {
    if (cp == U'\n') {
      pen_x = x;
      line_top += line_height;
      continue;
    }
    const auto it = atlas.glyphs.find(cp);
    if (it == atlas.glyphs.end()) {
      const float w = glyph_advance(cp, pixel_size);
      const float t = std::max(1.0f, pixel_size * 0.06f);
      const float bx = pen_x + w * 0.12f, bw = w * 0.76f;
      const float by = line_top + atlas.ascent * k - pixel_size * 0.7f, bh = pixel_size * 0.7f;
      draw_quad(renderer, white, bx, by, bw, t, color);
      draw_quad(renderer, white, bx, by + bh - t, bw, t, color);
      draw_quad(renderer, white, bx, by, t, bh, color);
      draw_quad(renderer, white, bx + bw - t, by, t, bh, color);
      pen_x += w;
      continue;
    }
    const AtlasGlyph& g = it->second;
    if (g.width > 0.0f && g.height > 0.0f) {
      renderer.draw(SpriteDraw{.texture = atlas_texture, .x = pen_x + g.x_offset * k,
                               .y = line_top + (atlas.ascent + g.y_offset) * k, .width = g.width * k,
                               .height = g.height * k, .origin_x = 0.0f, .origin_y = 0.0f, .tint = color,
                               .uv = UvRect{g.u0, g.v0, g.u1, g.v1}});
    }
    pen_x += g.advance * k;
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
  measure_text(text, hud_font_em * font_scale, out_w, out_h);
}

bool Hud::clicked(std::string_view id) const {
  return std::find(clicks_.begin(), clicks_.end(), id) != clicks_.end();
}

// ---- JSON ---------------------------------------------------------------

namespace {
constexpr const char* kHudFormat = "arcane.hud";
constexpr std::array<const char*, 9> kAnchorNames = {"top_left", "top", "top_right", "left", "center",
                                                     "right", "bottom_left", "bottom", "bottom_right"};

using nlohmann::json;

json color_to_json(const Color& c) { return json::array({c.r, c.g, c.b, c.a}); }

HudAnchor anchor_from_json(const json& j, HudAnchor fallback) {
  if (!j.contains("anchor")) return fallback;
  const std::string name = canonical::as_string(j["anchor"], "HUD 'anchor'");
  for (size_t i = 0; i < kAnchorNames.size(); ++i) {
    if (name == kAnchorNames[i]) return static_cast<HudAnchor>(i);
  }
  canonical::fail(std::format("unknown HUD anchor '{}'", name));
}

void put_layout(json& j, HudAnchor anchor, float ox, float oy) {
  j["anchor"] = kAnchorNames[static_cast<size_t>(anchor)];
  j["offset"] = json::array({ox, oy});
}

void get_offset(const json& j, float& ox, float& oy, const std::string& where) {
  if (!j.contains("offset")) return;
  const Vec2 v = canonical::as_vec2(j["offset"], where + " 'offset'");
  ox = v.x;
  oy = v.y;
}

void get_size(const json& j, float& w, float& h, const std::string& where) {
  if (!j.contains("size")) return;
  const Vec2 v = canonical::as_vec2(j["size"], where + " 'size'");
  w = v.x;
  h = v.y;
}

void get_color(const json& j, const char* key, Color& out, const std::string& where) {
  if (j.contains(key)) out = canonical::as_color(j[key], where + " '" + key + "'");
}

std::string& element_id(HudElement& element) {
  return std::visit([](auto& e) -> std::string& { return e.id; }, element);
}

const char* element_kind(const HudElement& element) {
  switch (element.index()) {
    case 0: return "text";
    case 1: return "image";
    case 2: return "bar";
    default: return "button";
  }
}

std::expected<Hud, std::string> load_hud(const std::string& text, bool strict_unknown) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed HUD JSON: {}", parsed.error()));
  try {
    const json& root = *parsed;
    canonical::require_object(root, "HUD");
    const canonical::Dialect dialect = canonical::detect_dialect(root, kHudFormat, "HUD");
    const bool v2 = dialect == canonical::Dialect::V2;
    const bool strict = strict_unknown || v2;
    if (strict) {
      if (v2) {
        canonical::require_only_keys(root, {"format", "version", "elements"}, "HUD");
      } else {
        canonical::require_only_keys(root, {"format_version", "elements"}, "HUD");
      }
    }
    Hud hud;
    if (!root.contains("elements")) return hud;
    if (!root["elements"].is_array()) return std::unexpected(std::string("HUD: 'elements' must be an array"));
    std::unordered_set<std::string> seen_ids;
    for (std::size_t i = 0; i < root["elements"].size(); ++i) {
      const json& j = root["elements"][i];
      const std::string where = std::format("HUD element {}", i);
      canonical::require_object(j, where);
      const std::string type = j.contains("type") ? canonical::as_string(j["type"], where + " 'type'") : std::string();
      std::string id;
      if (j.contains("id")) id = canonical::as_string(j["id"], where + " 'id'");
      if (v2 && id.empty()) return std::unexpected(std::format("HUD: {} needs a non-empty 'id'", where));
      if (strict && !id.empty() && !seen_ids.insert(id).second) {
        return std::unexpected(std::format("HUD: duplicate element id '{}'", id));
      }
      const HudAnchor anchor = anchor_from_json(j, HudAnchor::TopLeft);
      if (type == "text") {
        if (strict) {
          canonical::require_only_keys(j, {"type", "id", "text", "anchor", "offset", "font_scale", "color"}, where);
        }
        HudText t;
        t.id = id;
        if (j.contains("text")) t.text = canonical::as_string(j["text"], where + " 'text'");
        t.anchor = anchor;
        get_offset(j, t.offset_x, t.offset_y, where);
        if (j.contains("font_scale")) t.font_scale = canonical::as_float(j["font_scale"], where + " 'font_scale'");
        get_color(j, "color", t.color, where);
        hud.add(std::move(t));
      } else if (type == "image") {
        if (strict) {
          canonical::require_only_keys(j, {"type", "id", "asset", "uv", "anchor", "offset", "size", "tint"}, where);
        }
        HudImage image;
        image.id = id;
        if (j.contains("asset")) image.texture_asset = canonical::as_uint(j["asset"], where + " 'asset'");
        if (j.contains("uv")) {
          if (!j["uv"].is_array() || j["uv"].size() != 4) {
            return std::unexpected(std::format("HUD: {} 'uv' must be an array of 4 numbers", where));
          }
          image.uv = UvRect{canonical::as_float(j["uv"][0], where), canonical::as_float(j["uv"][1], where),
                            canonical::as_float(j["uv"][2], where), canonical::as_float(j["uv"][3], where)};
        }
        image.anchor = anchor;
        get_offset(j, image.offset_x, image.offset_y, where);
        get_size(j, image.width, image.height, where);
        get_color(j, "tint", image.tint, where);
        hud.add(std::move(image));
      } else if (type == "progress_bar") {
        if (strict) {
          canonical::require_only_keys(
              j, {"type", "id", "anchor", "offset", "size", "fraction", "fill", "background"}, where);
        }
        HudProgressBar bar;
        bar.id = id;
        bar.anchor = anchor;
        get_offset(j, bar.offset_x, bar.offset_y, where);
        get_size(j, bar.width, bar.height, where);
        if (j.contains("fraction")) bar.fraction = canonical::as_float(j["fraction"], where + " 'fraction'");
        get_color(j, "fill", bar.fill, where);
        get_color(j, "background", bar.background, where);
        hud.add(std::move(bar));
      } else if (type == "button") {
        if (strict) {
          canonical::require_only_keys(j,
                                       {"type", "id", "label", "anchor", "offset", "size", "font_scale", "enabled",
                                        "normal", "hover", "pressed", "disabled", "text_color"},
                                       where);
        }
        HudButton b;
        b.id = id;
        if (j.contains("label")) b.label = canonical::as_string(j["label"], where + " 'label'");
        b.anchor = j.contains("anchor") ? anchor : HudAnchor::Center;
        get_offset(j, b.offset_x, b.offset_y, where);
        get_size(j, b.width, b.height, where);
        if (j.contains("font_scale")) b.font_scale = canonical::as_float(j["font_scale"], where + " 'font_scale'");
        if (j.contains("enabled")) b.enabled = canonical::as_bool(j["enabled"], where + " 'enabled'");
        get_color(j, "normal", b.normal, where);
        get_color(j, "hover", b.hover, where);
        get_color(j, "pressed", b.pressed, where);
        get_color(j, "disabled", b.disabled, where);
        get_color(j, "text_color", b.text_color, where);
        hud.add(std::move(b));
      } else {
        return std::unexpected(std::format("unknown HUD element type '{}'", type));
      }
    }
    // Legacy files have no ids on text/bars (and maybe on buttons): assign
    // them deterministically in file order; v2 files are already complete.
    if (!v2) hud.ensure_unique_ids();
    return hud;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::format("HUD: {}", ex.what()));
  }
}
}  // namespace

namespace {
template <typename T>
T* find_by_id(std::vector<HudElement>& elements, std::string_view id) {
  for (HudElement& element : elements) {
    if (auto* found = std::get_if<T>(&element); found && found->id == id) return found;
  }
  return nullptr;
}
}  // namespace

HudText* Hud::find_text(std::string_view id) noexcept { return find_by_id<HudText>(elements_, id); }
HudImage* Hud::find_image(std::string_view id) noexcept { return find_by_id<HudImage>(elements_, id); }
HudProgressBar* Hud::find_bar(std::string_view id) noexcept { return find_by_id<HudProgressBar>(elements_, id); }
HudButton* Hud::find_button(std::string_view id) noexcept { return find_by_id<HudButton>(elements_, id); }

bool Hud::contains(std::string_view id) const {
  return std::any_of(elements_.begin(), elements_.end(), [&](const HudElement& e) {
    return std::visit([&](const auto& x) { return x.id == id; }, e);
  });
}

bool Hud::remove(std::string_view id) {
  const auto it = std::find_if(elements_.begin(), elements_.end(), [&](const HudElement& e) {
    return std::visit([&](const auto& x) { return x.id == id; }, e);
  });
  if (it == elements_.end()) return false;
  elements_.erase(it);
  return true;
}

void Hud::ensure_unique_ids() {
  std::unordered_set<std::string> used;
  std::vector<bool> needs_id(elements_.size(), false);
  for (std::size_t i = 0; i < elements_.size(); ++i) {
    const std::string& id = element_id(elements_[i]);
    needs_id[i] = id.empty() || !used.insert(id).second;
  }
  std::array<int, 4> next{1, 1, 1, 1};
  for (std::size_t i = 0; i < elements_.size(); ++i) {
    if (!needs_id[i]) continue;
    std::string id;
    do {
      id = std::format("{}_{}", element_kind(elements_[i]), next[elements_[i].index()]++);
    } while (!used.insert(id).second);
    element_id(elements_[i]) = std::move(id);
  }
}

std::expected<std::string, std::string> Hud::save_canonical_json() const {
  Hud copy;
  copy.elements_ = elements_;
  copy.ensure_unique_ids();
  json root;
  root["format"] = kHudFormat;
  root["version"] = 2;
  json list = json::array();
  for (const HudElement& element : copy.elements_) {
    json j;
    if (const auto* t = std::get_if<HudText>(&element)) {
      j["type"] = "text";
      j["id"] = t->id;
      j["text"] = t->text;
      put_layout(j, t->anchor, t->offset_x, t->offset_y);
      j["font_scale"] = t->font_scale;
      j["color"] = color_to_json(t->color);
    } else if (const auto* image = std::get_if<HudImage>(&element)) {
      j["type"] = "image";
      j["id"] = image->id;
      j["asset"] = image->texture_asset;
      j["uv"] = json::array({image->uv.u0, image->uv.v0, image->uv.u1, image->uv.v1});
      put_layout(j, image->anchor, image->offset_x, image->offset_y);
      j["size"] = json::array({image->width, image->height});
      j["tint"] = color_to_json(image->tint);
    } else if (const auto* bar = std::get_if<HudProgressBar>(&element)) {
      j["type"] = "progress_bar";
      j["id"] = bar->id;
      put_layout(j, bar->anchor, bar->offset_x, bar->offset_y);
      j["size"] = json::array({bar->width, bar->height});
      j["fraction"] = bar->fraction;
      j["fill"] = color_to_json(bar->fill);
      j["background"] = color_to_json(bar->background);
    } else {
      const auto& b = std::get<HudButton>(element);
      j["type"] = "button";
      j["id"] = b.id;
      j["label"] = b.label;
      put_layout(j, b.anchor, b.offset_x, b.offset_y);
      j["size"] = json::array({b.width, b.height});
      j["font_scale"] = b.font_scale;
      j["enabled"] = b.enabled;
      j["normal"] = color_to_json(b.normal);
      j["hover"] = color_to_json(b.hover);
      j["pressed"] = color_to_json(b.pressed);
      j["disabled"] = color_to_json(b.disabled);
      j["text_color"] = color_to_json(b.text_color);
    }
    list.push_back(std::move(j));
  }
  root["elements"] = std::move(list);
  return canonical::dump(root);
}

std::string Hud::save_to_json() const { return save_canonical_json().value_or(std::string()); }

std::expected<Hud, std::string> Hud::load_from_json(const std::string& text) { return load_hud(text, false); }

std::expected<Hud, std::string> Hud::load_from_json_strict(const std::string& text) { return load_hud(text, true); }

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

bool MaskedInputBackend::gamepad_button(int pad, int code) const {
  if (!inner_ || masked_.contains(pack_binding({InputDevice::GamepadButton, code}))) return false;
  return inner_->gamepad_button(pad, code);
}

float MaskedInputBackend::gamepad_axis(int pad, int axis) const {
  if (!inner_ || masked_.contains(pack_binding({InputDevice::GamepadAxis, axis}))) return 0.0f;
  return inner_->gamepad_axis(pad, axis);
}

bool HudInput::any_down(const InputBackend& backend, const std::vector<InputBinding>& bindings) {
  return std::any_of(bindings.begin(), bindings.end(),
                     [&](const InputBinding& b) { return backend.is_down(b.device, b.code); });
}

void HudInput::mask_all(const std::vector<InputBinding>& bindings) {
  for (const InputBinding& b : bindings) masked_.mask(b.device, b.code);
}

void HudInput::update(Hud& hud, const InputBackend& backend, int screen_width, int screen_height, Vec2 origin,
                      Vec2 pointer_scale) {
  hud.clicks_.clear();
  masked_ = MaskedInputBackend(backend);
  pointer_consumed_ = false;
  auto& elements = hud.elements_;
  const int count = static_cast<int>(elements.size());
  // Elements may have been removed since the last update: drop indices that no longer exist.
  if (focus_ >= count) focus_ = -1;
  if (pressed_button_ >= count) pressed_button_ = -1;
  if (last_hovered_ >= count) last_hovered_ = -1;

  auto button_at = [&](int i) -> HudButton* { return i >= 0 && i < count ? std::get_if<HudButton>(&elements[i]) : nullptr; };

  // Hit-test (topmost = last drawn wins).
  const Vec2 mouse = backend.mouse_position();
  const float px = (mouse.x - origin.x) * pointer_scale.x, py = (mouse.y - origin.y) * pointer_scale.y;
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
  FontAtlas atlas_data = build_font_atlas();
  if (atlas_data.rgba.empty()) return std::unexpected("failed to build the HUD font atlas");
  auto atlas = Texture::load_from_pixels(atlas_data.rgba, atlas_data.width, atlas_data.height, TextureFilter::Linear);
  if (!atlas) return std::unexpected(atlas.error());
  atlas_data.rgba = {};  // pixels live on the GPU now
  return HudRenderer(std::move(*texture), std::move(*atlas), std::move(atlas_data));
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
      draw_text(renderer, &white_, &atlas_, atlas_data_, text->text, r.x, r.y, hud_font_em * text->font_scale * scale, text->color);
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
      draw_text(renderer, &white_, &atlas_, atlas_data_, button->label, r.x + (r.w - tw * scale) * 0.5f, r.y + (r.h - th * scale) * 0.5f,
                hud_font_em * button->font_scale * scale, label_color);
    }
  }
}

}  // namespace arcane
