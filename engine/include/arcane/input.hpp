#pragma once

#include <cstddef>
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
enum class InputDevice { Keyboard = 0, MouseButton = 1, GamepadButton = 2, GamepadAxis = 3 };

inline constexpr int kMaxGamepads = 4;

// A single physical input bound to an action. `code` is a raw device code,
// not an arcane-owned enum: for Keyboard/MouseButton it is a GLFW key/mouse
// button code, for GamepadButton a GLFW_GAMEPAD_BUTTON_* index (see
// arcane::Keys/arcane::MouseButtons/arcane::GamepadButtons below for the
// common subset, so game code never has to #include GLFW itself). Kept as
// a plain int (not an enum class) so it round-trips through project.json
// like every other engine value -- see docs/decisions.md.
//
// Gamepad extras (ignored for Keyboard/MouseButton): `pad` selects one of
// kMaxGamepads pads, or -1 to defer to the action's `player` (and to "any
// pad" if that is -1 too). For GamepadAxis, `code` is a GLFW_GAMEPAD_AXIS_*
// index, `axis_sign` picks the direction (+1, -1, or 0 = either), values
// inside `deadzone` read as 0, and the binding counts as "down" once the
// rescaled magnitude reaches `threshold` (axis -> button).
struct InputBinding {
  InputDevice device = InputDevice::Keyboard;
  int code = 0;
  int pad = -1;
  int axis_sign = 0;
  float deadzone = 0.2f;
  float threshold = 0.5f;

  friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputAction {
  std::string name;
  std::vector<InputBinding> bindings;
  // Player index (gamepad number) whose pad this action reads when a gamepad
  // binding has no explicit pad; -1 = any pad.
  int player = -1;
};

// Per-axis scaled deadzone: |raw| <= deadzone -> 0, otherwise rescaled so
// the output still spans the full 0..1 range. Result in [-1, 1].
ARCANE_API float apply_deadzone(float raw, float deadzone) noexcept;

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
inline constexpr int kF3 = 292;
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

namespace GamepadAxes {
inline constexpr int kLeftX = 0;
inline constexpr int kLeftY = 1;
inline constexpr int kRightX = 2;
inline constexpr int kRightY = 3;
inline constexpr int kLeftTrigger = 4;
inline constexpr int kRightTrigger = 5;
inline constexpr int kCount = 6;
}  // namespace GamepadAxes

// Source of raw per-device state, polled once per frame by InputActionMap.
// Abstracted so action-map/queries are unit-testable without a real window
// or input devices (see FakeInputBackend), and so the only place that
// touches GLFW's input functions is GlfwInputBackend.
class ARCANE_API InputBackend {
 public:
  virtual ~InputBackend() = default;
  // Keyboard/mouse state; for GamepadButton, true if ANY pad has it down.
  virtual bool is_down(InputDevice device, int code) const = 0;
  virtual Vec2 mouse_position() const = 0;
  // Per-pad state (pad in [0, kMaxGamepads)); disconnected pads read as
  // up / 0. Axis values are in [-1, 1].
  virtual bool gamepad_button(int pad, int code) const;
  virtual float gamepad_axis(int pad, int axis) const;
};

class Window;

// Polls a real arcane::Window's GLFW handle. Pad N is GLFW joystick N
// (GLFW_JOYSTICK_1 + N), up to kMaxGamepads. Triggers are reported as GLFW
// mapped to 0 at rest and +1 fully pressed.
class ARCANE_API GlfwInputBackend : public InputBackend {
 public:
  explicit GlfwInputBackend(Window& window) noexcept : window_(&window) {}

  bool is_down(InputDevice device, int code) const override;
  Vec2 mouse_position() const override;
  bool gamepad_button(int pad, int code) const override;
  float gamepad_axis(int pad, int axis) const override;

 private:
  Window* window_;
};

// Lets tests set device state directly, with no window/platform input at
// all.
class ARCANE_API FakeInputBackend : public InputBackend {
 public:
  // set_down(GamepadButton, ...) is shorthand for pad 0.
  void set_down(InputDevice device, int code, bool down);
  void set_gamepad_button(int pad, int code, bool down);
  void set_gamepad_axis(int pad, int axis, float value);
  void set_mouse_position(Vec2 position) noexcept { mouse_position_ = position; }

  bool is_down(InputDevice device, int code) const override;
  Vec2 mouse_position() const override { return mouse_position_; }
  bool gamepad_button(int pad, int code) const override;
  float gamepad_axis(int pad, int axis) const override;

 private:
  std::unordered_map<std::uint64_t, bool> down_;
  std::unordered_map<std::uint64_t, bool> buttons_;
  std::unordered_map<std::uint64_t, float> axes_;
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
  bool remove_action(std::string_view action_name);
  void add_binding(std::string_view action_name, InputBinding binding);
  void clear_bindings(std::string_view action_name);
  // Removes bindings[slot] of the action; false if out of range/unknown.
  bool remove_binding(std::string_view action_name, std::size_t slot);
  // Replaces bindings[slot]; false if out of range/unknown.
  bool replace_binding(std::string_view action_name, std::size_t slot, InputBinding binding);
  void set_player(std::string_view action_name, int player);
  // Names of OTHER actions that already use a binding that would fire on the
  // same physical input as `binding` for `action_name` (same device/code/
  // axis direction, and overlapping pads given each action's player).
  std::vector<std::string> find_conflicts(std::string_view action_name, const InputBinding& binding) const;
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
  // Analog magnitude in [0, 1] (digital bindings contribute 0 or 1; the
  // strongest binding wins). 0 for an unknown action.
  float value(std::string_view action_name) const;
  // value(positive) - value(negative): a signed axis built from two actions.
  float axis(std::string_view negative, std::string_view positive) const;
  Vec2 mouse_position() const noexcept { return mouse_position_; }

  // Only the action/binding config round-trips (canonical `arcane.input`
  // v2, docs/formats/input.md; legacy format_version 1 still loads);
  // per-frame held/pressed/released state does not.
  std::expected<std::string, std::string> save_canonical_json() const;
  // Convenience wrapper; returns an empty string on failure.
  std::string save_to_json() const;
  static std::expected<InputActionMap, std::string> load_from_json(const std::string& text);
  // Legacy files are checked as strictly as v2 ones (no clamping, no unknown fields); used by migration.
  static std::expected<InputActionMap, std::string> load_from_json_strict(const std::string& text);

 private:
  struct ActionState {
    bool down = false;
    bool down_previous = false;
    float value = 0.0f;
  };

  std::vector<InputAction> actions_;
  std::unordered_map<std::string, ActionState> state_;
  Vec2 mouse_position_{};
};

// Every physical input currently active on `backend` as a binding (keys,
// mouse buttons, gamepad buttons, and axes deflected past 0.6). Gamepad
// results use pad = -1 (player routing is the action's `player`).
ARCANE_API std::vector<InputBinding> detect_active_inputs(const InputBackend& backend);

// Runtime "press the new input" rebinding with conflict detection. Usage:
// begin(map, "jump"); each frame rebinder.update(backend). The first input
// pressed AFTER begin (inputs already held are ignored) becomes the binding;
// Escape cancels. If another action already uses it the state becomes
// Conflict and the caller must resolve(); otherwise it is applied at once.
class ARCANE_API InputRebinder {
 public:
  enum class State { Idle, Listening, Conflict };
  enum class Resolution { Replace, Allow, Cancel };
  static constexpr std::size_t kAppend = static_cast<std::size_t>(-1);

  // Starts listening for `action_name` (created if missing). `slot` replaces
  // that existing binding, kAppend adds a new one. `map` must outlive the
  // listening session.
  void begin(InputActionMap& map, std::string action_name, std::size_t slot = kAppend);
  void cancel() noexcept;
  void update(const InputBackend& backend);
  // Replace: strip the binding from the conflicting actions, then apply.
  // Allow: apply anyway (both actions fire). Cancel: discard.
  void resolve(Resolution resolution);

  State state() const noexcept { return state_; }
  const InputBinding& pending() const noexcept { return pending_; }
  const std::vector<std::string>& conflicts() const noexcept { return conflicts_; }

 private:
  void apply();

  State state_ = State::Idle;
  InputActionMap* map_ = nullptr;
  std::string action_;
  std::size_t slot_ = kAppend;
  bool have_baseline_ = false;
  std::vector<InputBinding> baseline_;
  InputBinding pending_;
  std::vector<std::string> conflicts_;
};

// The InputActionMap live for the current frame, set by the player/editor
// main loop before each frame's fixed-step batch so gameplay code (running
// inside Component::on_update or GameApi::on_update) can reach it without
// every call site threading a reference through. Null outside Play (e.g.
// the editor in Edit mode never sets one). See docs/decisions.md.
ARCANE_API void set_active_input_map(InputActionMap* map) noexcept;
ARCANE_API InputActionMap* active_input_map() noexcept;

}  // namespace arcane
