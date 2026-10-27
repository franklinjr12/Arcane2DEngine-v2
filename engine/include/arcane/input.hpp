#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arcane/export.hpp"
#include "arcane/math2d.hpp"

namespace arcane {

// Which physical device family a binding's `code` is interpreted against.
enum class InputDevice { Keyboard = 0, MouseButton = 1, GamepadButton = 2 };

// A single physical input bound to an action. `code` is a raw device code,
// not an arcane-owned enum: for Keyboard/MouseButton it is a GLFW key/mouse
// button code, for GamepadButton a GLFW_GAMEPAD_BUTTON_* index (see
// arcane::Keys/arcane::MouseButtons/arcane::GamepadButtons below for the
// common subset, so game code never has to #include GLFW itself). Kept as
// a plain int (not an enum class) so it round-trips through project.json
// like every other engine value -- see docs/decisions.md.
struct InputBinding {
  InputDevice device = InputDevice::Keyboard;
  int code = 0;

  friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputAction {
  std::string name;
  std::vector<InputBinding> bindings;
};

// Named subset of GLFW's key/button codes, duplicated here (values are part
// of GLFW's stable public C API) so binding an action never requires
// pulling the GLFW header into game code. Not exhaustive -- add more as
// games need them.
namespace Keys {
inline constexpr int kSpace = 32;
inline constexpr int kA = 65;
inline constexpr int kD = 68;
inline constexpr int kS = 83;
inline constexpr int kW = 87;
inline constexpr int kEscape = 256;
inline constexpr int kEnter = 257;
inline constexpr int kRight = 262;
inline constexpr int kLeft = 263;
inline constexpr int kDown = 264;
inline constexpr int kUp = 265;
inline constexpr int kLeftShift = 340;
}  // namespace Keys

namespace MouseButtons {
inline constexpr int kLeft = 0;
inline constexpr int kRight = 1;
inline constexpr int kMiddle = 2;
}  // namespace MouseButtons

namespace GamepadButtons {
inline constexpr int kA = 0;
inline constexpr int kB = 1;
inline constexpr int kX = 2;
inline constexpr int kY = 3;
inline constexpr int kDpadUp = 11;
inline constexpr int kDpadRight = 12;
inline constexpr int kDpadDown = 13;
inline constexpr int kDpadLeft = 14;
}  // namespace GamepadButtons

// Source of raw per-device state, polled once per frame by InputActionMap.
// Abstracted so action-map/queries are unit-testable without a real window
// or input devices (see FakeInputBackend), and so the only place that
// touches GLFW's input functions is GlfwInputBackend.
class InputBackend {
 public:
  virtual ~InputBackend() = default;
  virtual bool is_down(InputDevice device, int code) const = 0;
  virtual Vec2 mouse_position() const = 0;
};

class Window;

// Polls a real arcane::Window's GLFW handle. Gamepad codes are read from
// GLFW's first connected gamepad (joystick 0); no multi-gamepad support
// yet.
class ARCANE_API GlfwInputBackend : public InputBackend {
 public:
  explicit GlfwInputBackend(Window& window) noexcept : window_(&window) {}

  bool is_down(InputDevice device, int code) const override;
  Vec2 mouse_position() const override;

 private:
  Window* window_;
};

// Lets tests set device state directly, with no window/platform input at
// all.
class ARCANE_API FakeInputBackend : public InputBackend {
 public:
  void set_down(InputDevice device, int code, bool down);
  void set_mouse_position(Vec2 position) noexcept { mouse_position_ = position; }

  bool is_down(InputDevice device, int code) const override;
  Vec2 mouse_position() const override { return mouse_position_; }

 private:
  std::unordered_map<std::uint64_t, bool> down_;
  Vec2 mouse_position_{};
};

// Named actions, each bound to zero or more physical inputs; gameplay code
// queries actions by name instead of hard-coding device codes (doc section
// 7.4). Not thread-safe; expected to be updated and queried from the main
// loop only.
class ARCANE_API InputActionMap {
 public:
  // Replaces an existing action with the same name, or adds a new one.
  // Resets that action's pressed/held/released state.
  void set_action(InputAction action);
  void add_binding(std::string_view action_name, InputBinding binding);
  void clear_bindings(std::string_view action_name);
  const std::vector<InputAction>& actions() const noexcept { return actions_; }

  // Polls `backend` for every bound device code and updates held/pressed/
  // released state. Call exactly once per real frame (not once per fixed
  // step) before querying -- edge detection (pressed/released) compares
  // against the previous call.
  void update(const InputBackend& backend);

  // False (not an error) for an unknown action name, since a missing
  // binding should read as "not pressed" rather than crash gameplay code.
  bool held(std::string_view action_name) const;
  bool pressed(std::string_view action_name) const;
  bool released(std::string_view action_name) const;
  Vec2 mouse_position() const noexcept { return mouse_position_; }

  // Only the action/binding config round-trips (format_version 1, see
  // docs/decisions.md); per-frame held/pressed/released state does not.
  std::string save_to_json() const;
  static std::expected<InputActionMap, std::string> load_from_json(const std::string& text);

 private:
  struct ActionState {
    bool down = false;
    bool down_previous = false;
  };

  std::vector<InputAction> actions_;
  std::unordered_map<std::string, ActionState> state_;
  Vec2 mouse_position_{};
};

// The InputActionMap live for the current frame, set by the player/editor
// main loop before each frame's fixed-step batch so gameplay code (running
// inside Component::on_update or GameApi::on_update) can reach it without
// every call site threading a reference through. Null outside Play (e.g.
// the editor in Edit mode never sets one). See docs/decisions.md.
ARCANE_API void set_active_input_map(InputActionMap* map) noexcept;
ARCANE_API InputActionMap* active_input_map() noexcept;

}  // namespace arcane
