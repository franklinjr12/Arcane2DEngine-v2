#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

#include "arcane/export.hpp"
#include "arcane/input.hpp"
#include "arcane/renderer.hpp"
#include "arcane/text.hpp"

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

// Text is UTF-8 (Latin-1 + Latin Extended-A glyphs, '\n' for new lines; see
// text.hpp). font_scale 1 = an em of `hud_font_em` layout units.
inline constexpr float hud_font_em = 8.0f;

// Size of `text` in layout units when drawn at `font_scale`.
ARCANE_API void hud_text_size(const std::string& text, float font_scale, float& out_w, float& out_h) noexcept;

// Every element carries a stable string `id` (unique within one Hud; button
// ids are also what game code listens for). Elements built in code may leave
// it empty: saving assigns "<kind>_<n>" ids (Hud::ensure_unique_ids).
struct HudText {
  std::string id;
  std::string text;
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float font_scale = 2.0f;
  Color color{};
};

struct HudImage {
  std::string id;
  // The persisted reference: a project texture asset id (0 = none). `texture`
  // is the runtime pointer resolved from it (or set directly by game code);
  // it is never saved.
  std::uint64_t texture_asset = 0;
  const Texture* texture = nullptr;
  UvRect uv{};
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float width = 32.0f, height = 32.0f;
  Color tint{};
};

struct HudProgressBar {
  std::string id;
  HudAnchor anchor = HudAnchor::TopLeft;
  float offset_x = 0.0f, offset_y = 0.0f;
  float width = 200.0f, height = 16.0f;
  float fraction = 1.0f;  // clamped to [0, 1]
  Color fill{0.2f, 0.8f, 0.3f, 1.0f};
  Color background{0.0f, 0.0f, 0.0f, 0.6f};
};

enum class HudButtonState { Normal, Hover, Pressed, Disabled };

// Clickable rectangle with a centered label. `id` is how game code learns
// about clicks (Hud::set_click_handler / Hud::clicked). `state` is runtime
// state recomputed by HudInput every frame; it is not serialized.
struct HudButton {
  std::string id;
  std::string label;
  HudAnchor anchor = HudAnchor::Center;
  float offset_x = 0.0f, offset_y = 0.0f;
  float width = 160.0f, height = 40.0f;
  float font_scale = 2.0f;
  bool enabled = true;
  Color normal{0.2f, 0.2f, 0.25f, 0.9f};
  Color hover{0.3f, 0.3f, 0.4f, 0.95f};
  Color pressed{0.1f, 0.1f, 0.15f, 1.0f};
  Color disabled{0.15f, 0.15f, 0.15f, 0.5f};
  Color text_color{1.0f, 1.0f, 1.0f, 1.0f};
  HudButtonState state = HudButtonState::Normal;
  bool focused = false;  // runtime, drawn with an outline
};

using HudElement = std::variant<HudText, HudImage, HudProgressBar, HudButton>;

// Retained list of HUD elements, drawn in insertion order.
class ARCANE_API Hud {
 public:
  void add(HudElement element) { elements_.push_back(std::move(element)); }
  void clear() noexcept { elements_.clear(); }
  // Removes the element with this id, of any kind; false if none. Invalidates pointers and indices (HudInput
  // re-validates its focus on the next update).
  bool remove(std::string_view id);
  bool contains(std::string_view id) const;
  const std::vector<HudElement>& elements() const noexcept { return elements_; }
  std::vector<HudElement>& elements() noexcept { return elements_; }

  // Called by HudInput (once per real frame) for each click, before the
  // frame's fixed steps. Prefer this over polling clicked() when a click
  // must act exactly once.
  void set_click_handler(std::function<void(std::string_view)> handler) { click_handler_ = std::move(handler); }
  // Button ids clicked during the most recent HudInput::update.
  bool clicked(std::string_view id) const;
  const std::vector<std::string>& clicks() const noexcept { return clicks_; }

  // The element of that kind with this id, or null (ids are unique within a Hud, so a wrong kind finds nothing).
  // Pointers stay valid until elements are added or removed.
  HudText* find_text(std::string_view id) noexcept;
  HudImage* find_image(std::string_view id) noexcept;
  HudProgressBar* find_bar(std::string_view id) noexcept;
  HudButton* find_button(std::string_view id) noexcept;

  // Gives every element with an empty or repeated id a fresh "<kind>_<n>" id
  // (text_1, image_1, bar_1, button_1, ... skipping ids in use), in order.
  void ensure_unique_ids();

  // Canonical `arcane.hud` v2 (docs/formats/hud.md): layout, ids and image
  // asset references; runtime state (button state, focus, texture pointers)
  // is not saved. Legacy format_version 1 still loads.
  std::expected<std::string, std::string> save_canonical_json() const;
  // Convenience wrapper; returns an empty string on failure.
  std::string save_to_json() const;
  static std::expected<Hud, std::string> load_from_json(const std::string& text);
  // Legacy files are checked as strictly as v2 ones; used by migration.
  static std::expected<Hud, std::string> load_from_json_strict(const std::string& text);

 private:
  friend class HudInput;
  std::vector<HudElement> elements_;
  std::vector<std::string> clicks_;
  std::function<void(std::string_view)> click_handler_;
};

// A view of another InputBackend with some device codes forced "up"; this is
// how consumed HUD input is hidden from gameplay actions.
class ARCANE_API MaskedInputBackend : public InputBackend {
 public:
  MaskedInputBackend() = default;  // no inner backend: everything reads as up
  explicit MaskedInputBackend(const InputBackend& inner) noexcept : inner_(&inner) {}
  void mask(InputDevice device, int code);
  bool is_down(InputDevice device, int code) const override;
  Vec2 mouse_position() const override { return inner_ ? inner_->mouse_position() : Vec2{}; }
  bool gamepad_button(int pad, int code) const override;
  float gamepad_axis(int pad, int axis) const override;

 private:
  const InputBackend* inner_ = nullptr;
  std::unordered_set<std::uint64_t> masked_;
};

// Routes input to the HUD first each frame (doc section 7.7). Usage per real
// frame:  hud_input.update(hud, backend, w, h);  action_map.update(
// hud_input.gameplay_backend());  Inputs the HUD consumed read as "not down"
// for gameplay actions: a mouse press that starts on a button (until it is
// released), and nav/confirm keys while a button has focus.
class ARCANE_API HudInput {
 public:
  HudInput() = default;

  // Pointer position is offset by `origin` (e.g. the editor viewport's
  // top-left) before hit-testing in HUD pixels.
  void update(Hud& hud, const InputBackend& backend, int screen_width, int screen_height, Vec2 origin = {});

  // Valid until the next update() (or until this HudInput is moved/copied).
  const InputBackend& gameplay_backend() const noexcept { return masked_; }
  bool consumed_pointer() const noexcept { return pointer_consumed_; }

  // Bindings that move focus / activate the focused button.
  std::vector<InputBinding> next_bindings{{InputDevice::Keyboard, 264}, {InputDevice::Keyboard, 258},
                                          {InputDevice::GamepadButton, GamepadButtons::kDpadDown}};
  std::vector<InputBinding> previous_bindings{{InputDevice::Keyboard, 265},
                                              {InputDevice::GamepadButton, GamepadButtons::kDpadUp}};
  std::vector<InputBinding> confirm_bindings{{InputDevice::Keyboard, Keys::kEnter},
                                             {InputDevice::GamepadButton, GamepadButtons::kA}};
  int pointer_button = MouseButtons::kLeft;

  int focus_index() const noexcept { return focus_; }  // index into Hud::elements(), -1 none

 private:
  static bool any_down(const InputBackend& backend, const std::vector<InputBinding>& bindings);
  void mask_all(const std::vector<InputBinding>& bindings);

  MaskedInputBackend masked_;
  int focus_ = -1;
  bool focus_from_pointer_ = false;
  int last_hovered_ = -1;
  int pressed_button_ = -1;  // element index of a pointer press that began on a button
  bool pointer_was_down_ = false;
  bool next_was_down_ = false, previous_was_down_ = false, confirm_was_down_ = false;
  bool pointer_consumed_ = false;
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
  HudRenderer(Texture white, Texture atlas, FontAtlas atlas_data)
      : white_(std::move(white)), atlas_(std::move(atlas)), atlas_data_(std::move(atlas_data)) {}
  Texture white_;
  Texture atlas_;
  FontAtlas atlas_data_;
};

}  // namespace arcane
