#include "arcane/input.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <json.hpp>

#include "arcane/window.hpp"

namespace arcane {

namespace {
constexpr int kInputFormatVersion = 1;

const char* device_to_string(InputDevice device) {
  switch (device) {
    case InputDevice::Keyboard:
      return "keyboard";
    case InputDevice::MouseButton:
      return "mouse_button";
    case InputDevice::GamepadButton:
      return "gamepad_button";
    case InputDevice::GamepadAxis:
      return "gamepad_axis";
  }
  return "keyboard";
}

std::expected<InputDevice, std::string> device_from_string(const std::string& text) {
  if (text == "keyboard") return InputDevice::Keyboard;
  if (text == "mouse_button") return InputDevice::MouseButton;
  if (text == "gamepad_button") return InputDevice::GamepadButton;
  if (text == "gamepad_axis") return InputDevice::GamepadAxis;
  return std::unexpected(std::format("unknown input device '{}'", text));
}

// Packs device+code into one map key; collisions are impossible since code
// is restricted to small non-negative values in practice (GLFW codes) and
// device only ever takes 3 values.
std::uint64_t pack(InputDevice device, int code) noexcept {
  return (static_cast<std::uint64_t>(static_cast<int>(device)) << 32) | static_cast<std::uint32_t>(code);
}

std::uint64_t pack_pad(int pad, int code) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pad)) << 32) | static_cast<std::uint32_t>(code);
}

bool valid_pad(int pad) noexcept { return pad >= 0 && pad < kMaxGamepads; }

// Rescaled magnitude (0..1) of one binding on one pad, or on every pad when
// `pad` < 0 (strongest wins). Digital bindings give 0 or 1.
float binding_value(const InputBackend& backend, const InputBinding& b, int pad) {
  switch (b.device) {
    case InputDevice::Keyboard:
    case InputDevice::MouseButton:
      return backend.is_down(b.device, b.code) ? 1.0f : 0.0f;
    case InputDevice::GamepadButton:
      if (pad < 0) return backend.is_down(b.device, b.code) ? 1.0f : 0.0f;
      return backend.gamepad_button(pad, b.code) ? 1.0f : 0.0f;
    case InputDevice::GamepadAxis: {
      float best = 0.0f;
      for (int p = pad < 0 ? 0 : pad; p < (pad < 0 ? kMaxGamepads : pad + 1); ++p) {
        const float v = apply_deadzone(backend.gamepad_axis(p, b.code), b.deadzone);
        const float mag = b.axis_sign > 0 ? v : b.axis_sign < 0 ? -v : std::fabs(v);
        best = std::max(best, mag);
      }
      return best;
    }
  }
  return 0.0f;
}

bool binding_down(float value, const InputBinding& b) {
  if (b.device != InputDevice::GamepadAxis) return value > 0.0f;
  return value > 0.0f && value >= b.threshold;
}

int effective_pad(const InputBinding& b, const InputAction& action) { return b.pad >= 0 ? b.pad : action.player; }

bool pads_overlap(int a, int b) { return a < 0 || b < 0 || a == b; }
}  // namespace

float apply_deadzone(float raw, float deadzone) noexcept {
  deadzone = std::clamp(deadzone, 0.0f, 0.99f);
  const float mag = std::fabs(raw);
  if (mag <= deadzone) return 0.0f;
  const float scaled = std::min((mag - deadzone) / (1.0f - deadzone), 1.0f);
  return raw < 0.0f ? -scaled : scaled;
}

bool InputBackend::gamepad_button(int, int) const { return false; }
float InputBackend::gamepad_axis(int, int) const { return 0.0f; }

bool GlfwInputBackend::gamepad_button(int pad, int code) const {
  if (!valid_pad(pad) || code < 0 || code > GLFW_GAMEPAD_BUTTON_LAST) return false;
  GLFWgamepadstate state;
  if (!glfwGetGamepadState(GLFW_JOYSTICK_1 + pad, &state)) return false;
  return state.buttons[code] == GLFW_PRESS;
}

float GlfwInputBackend::gamepad_axis(int pad, int axis) const {
  if (!valid_pad(pad) || axis < 0 || axis > GLFW_GAMEPAD_AXIS_LAST) return 0.0f;
  GLFWgamepadstate state;
  if (!glfwGetGamepadState(GLFW_JOYSTICK_1 + pad, &state)) return 0.0f;
  return state.axes[axis];
}

bool GlfwInputBackend::is_down(InputDevice device, int code) const {
  auto* handle = static_cast<GLFWwindow*>(window_->native_handle());
  switch (device) {
    case InputDevice::Keyboard:
      return glfwGetKey(handle, code) == GLFW_PRESS;
    case InputDevice::MouseButton:
      return glfwGetMouseButton(handle, code) == GLFW_PRESS;
    case InputDevice::GamepadButton:
      for (int pad = 0; pad < kMaxGamepads; ++pad) {
        if (gamepad_button(pad, code)) return true;
      }
      return false;
    case InputDevice::GamepadAxis:
      return false;  // axes are read through gamepad_axis()
  }
  return false;
}

Vec2 GlfwInputBackend::mouse_position() const {
  auto* handle = static_cast<GLFWwindow*>(window_->native_handle());
  double x = 0.0, y = 0.0;
  glfwGetCursorPos(handle, &x, &y);
  return {static_cast<float>(x), static_cast<float>(y)};
}

void FakeInputBackend::set_down(InputDevice device, int code, bool down) {
  if (device == InputDevice::GamepadButton) {
    set_gamepad_button(0, code, down);
    return;
  }
  down_[pack(device, code)] = down;
}

void FakeInputBackend::set_gamepad_button(int pad, int code, bool down) { down_[pack_pad(pad + 1, code)] = down; }
void FakeInputBackend::set_gamepad_axis(int pad, int axis, float value) { axes_[pack_pad(pad, axis)] = value; }

bool FakeInputBackend::gamepad_button(int pad, int code) const {
  auto it = down_.find(pack_pad(pad + 1, code));
  return it != down_.end() && it->second;
}

float FakeInputBackend::gamepad_axis(int pad, int axis) const {
  auto it = axes_.find(pack_pad(pad, axis));
  return it != axes_.end() ? it->second : 0.0f;
}

bool FakeInputBackend::is_down(InputDevice device, int code) const {
  if (device == InputDevice::GamepadButton) {
    for (int pad = 0; pad < kMaxGamepads; ++pad) {
      if (gamepad_button(pad, code)) return true;
    }
    return false;
  }
  auto it = down_.find(pack(device, code));
  return it != down_.end() && it->second;
}

void InputActionMap::set_action(InputAction action) {
  state_[action.name] = ActionState{};
  for (auto& existing : actions_) {
    if (existing.name == action.name) {
      existing = std::move(action);
      return;
    }
  }
  actions_.push_back(std::move(action));
}

void InputActionMap::add_binding(std::string_view action_name, InputBinding binding) {
  for (auto& action : actions_) {
    if (action.name == action_name) {
      action.bindings.push_back(binding);
      return;
    }
  }
  InputAction action;
  action.name = std::string(action_name);
  action.bindings.push_back(binding);
  set_action(std::move(action));
}

void InputActionMap::clear_bindings(std::string_view action_name) {
  for (auto& action : actions_) {
    if (action.name == action_name) {
      action.bindings.clear();
      return;
    }
  }
}

void InputActionMap::update(const InputBackend& backend) {
  for (const auto& action : actions_) {
    bool down = false;
    float value = 0.0f;
    for (const auto& binding : action.bindings) {
      const float v = binding_value(backend, binding, effective_pad(binding, action));
      if (binding_down(v, binding)) {
        down = true;
        value = std::max(value, v);
      }
    }
    auto& state = state_[action.name];
    state.down_previous = state.down;
    state.down = down;
    state.value = value;
  }
  mouse_position_ = backend.mouse_position();
}

bool InputActionMap::held(std::string_view action_name) const {
  auto it = state_.find(std::string(action_name));
  return it != state_.end() && it->second.down;
}

bool InputActionMap::pressed(std::string_view action_name) const {
  auto it = state_.find(std::string(action_name));
  return it != state_.end() && it->second.down && !it->second.down_previous;
}

bool InputActionMap::released(std::string_view action_name) const {
  auto it = state_.find(std::string(action_name));
  return it != state_.end() && !it->second.down && it->second.down_previous;
}

float InputActionMap::value(std::string_view action_name) const {
  auto it = state_.find(std::string(action_name));
  return it != state_.end() ? it->second.value : 0.0f;
}

float InputActionMap::axis(std::string_view negative, std::string_view positive) const {
  return value(positive) - value(negative);
}

bool InputActionMap::remove_binding(std::string_view action_name, std::size_t slot) {
  for (auto& action : actions_) {
    if (action.name != action_name) continue;
    if (slot >= action.bindings.size()) return false;
    action.bindings.erase(action.bindings.begin() + static_cast<std::ptrdiff_t>(slot));
    return true;
  }
  return false;
}

bool InputActionMap::replace_binding(std::string_view action_name, std::size_t slot, InputBinding binding) {
  for (auto& action : actions_) {
    if (action.name != action_name) continue;
    if (slot >= action.bindings.size()) return false;
    action.bindings[slot] = binding;
    return true;
  }
  return false;
}

void InputActionMap::set_player(std::string_view action_name, int player) {
  for (auto& action : actions_) {
    if (action.name == action_name) action.player = std::clamp(player, -1, kMaxGamepads - 1);
  }
}

std::vector<std::string> InputActionMap::find_conflicts(std::string_view action_name, const InputBinding& binding) const {
  const InputAction* self = nullptr;
  for (const auto& action : actions_) {
    if (action.name == action_name) self = &action;
  }
  const int self_pad = binding.pad >= 0 ? binding.pad : (self ? self->player : -1);
  const bool is_pad = binding.device == InputDevice::GamepadButton || binding.device == InputDevice::GamepadAxis;

  std::vector<std::string> result;
  for (const auto& action : actions_) {
    if (action.name == action_name) continue;
    for (const auto& other : action.bindings) {
      if (other.device != binding.device || other.code != binding.code) continue;
      if (binding.device == InputDevice::GamepadAxis && other.axis_sign != binding.axis_sign &&
          other.axis_sign != 0 && binding.axis_sign != 0) {
        continue;  // opposite directions of one axis do not clash
      }
      if (is_pad && !pads_overlap(self_pad, effective_pad(other, action))) continue;
      result.push_back(action.name);
      break;
    }
  }
  return result;
}

std::string InputActionMap::save_to_json() const {
  nlohmann::json root;
  root["format_version"] = kInputFormatVersion;
  nlohmann::json actions_json = nlohmann::json::array();
  for (const auto& action : actions_) {
    nlohmann::json action_json;
    action_json["name"] = action.name;
    nlohmann::json bindings_json = nlohmann::json::array();
    for (const auto& binding : action.bindings) {
      nlohmann::json binding_json = {{"device", device_to_string(binding.device)}, {"code", binding.code}};
      // Gamepad extras are written only when non-default, so files without
      // them stay byte-identical to the original format.
      const InputBinding defaults;
      if (binding.pad != defaults.pad) binding_json["pad"] = binding.pad;
      if (binding.device == InputDevice::GamepadAxis) {
        binding_json["axis_sign"] = binding.axis_sign;
        binding_json["deadzone"] = binding.deadzone;
        binding_json["threshold"] = binding.threshold;
      }
      bindings_json.push_back(std::move(binding_json));
    }
    if (action.player != -1) action_json["player"] = action.player;
    action_json["bindings"] = std::move(bindings_json);
    actions_json.push_back(std::move(action_json));
  }
  root["actions"] = std::move(actions_json);
  return root.dump(2);
}

std::expected<InputActionMap, std::string> InputActionMap::load_from_json(const std::string& text) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error& ex) {
    return std::unexpected(std::format("malformed input map JSON: {}", ex.what()));
  }

  if (!root.contains("format_version") || !root["format_version"].is_number_integer()) {
    return std::unexpected(std::string("input map JSON is missing a numeric 'format_version' field"));
  }
  const int file_version = root["format_version"].get<int>();
  if (file_version != kInputFormatVersion) {
    return std::unexpected(std::format("unsupported input map format_version {} (this build only supports version {})",
                                        file_version, kInputFormatVersion));
  }

  InputActionMap map;
  for (const auto& action_json : root.value("actions", nlohmann::json::array())) {
    InputAction action;
    action.name = action_json.value("name", std::string());
    for (const auto& binding_json : action_json.value("bindings", nlohmann::json::array())) {
      auto device = device_from_string(binding_json.value("device", std::string()));
      if (!device) return std::unexpected(device.error());
      InputBinding binding;
      binding.device = *device;
      binding.code = binding_json.value("code", 0);
      binding.pad = std::clamp(binding_json.value("pad", -1), -1, kMaxGamepads - 1);
      binding.axis_sign = std::clamp(binding_json.value("axis_sign", 0), -1, 1);
      binding.deadzone = std::clamp(binding_json.value("deadzone", binding.deadzone), 0.0f, 0.99f);
      binding.threshold = std::clamp(binding_json.value("threshold", binding.threshold), 0.0f, 1.0f);
      action.bindings.push_back(binding);
    }
    action.player = std::clamp(action_json.value("player", -1), -1, kMaxGamepads - 1);
    map.set_action(std::move(action));
  }
  return map;
}

std::vector<InputBinding> detect_active_inputs(const InputBackend& backend) {
  std::vector<InputBinding> result;
  for (int key = 32; key <= 348; ++key) {
    if (backend.is_down(InputDevice::Keyboard, key)) result.push_back({InputDevice::Keyboard, key});
  }
  for (int button = 0; button <= 7; ++button) {
    if (backend.is_down(InputDevice::MouseButton, button)) result.push_back({InputDevice::MouseButton, button});
  }
  for (int button = 0; button <= 14; ++button) {
    if (backend.is_down(InputDevice::GamepadButton, button)) result.push_back({InputDevice::GamepadButton, button});
  }
  constexpr float kAxisDetect = 0.6f;
  for (int axis = 0; axis < GamepadAxes::kCount; ++axis) {
    for (int pad = 0; pad < kMaxGamepads; ++pad) {
      const float v = backend.gamepad_axis(pad, axis);
      if (std::fabs(v) < kAxisDetect) continue;
      InputBinding binding{InputDevice::GamepadAxis, axis};
      binding.axis_sign = v > 0.0f ? 1 : -1;
      if (std::find(result.begin(), result.end(), binding) == result.end()) result.push_back(binding);
    }
  }
  return result;
}

void InputRebinder::begin(InputActionMap& map, std::string action_name, std::size_t slot) {
  map_ = &map;
  action_ = std::move(action_name);
  slot_ = slot;
  have_baseline_ = false;
  baseline_.clear();
  conflicts_.clear();
  state_ = State::Listening;
}

void InputRebinder::cancel() noexcept {
  state_ = State::Idle;
  map_ = nullptr;
  conflicts_.clear();
}

void InputRebinder::update(const InputBackend& backend) {
  if (state_ != State::Listening) return;
  std::vector<InputBinding> active = detect_active_inputs(backend);
  if (!have_baseline_) {  // whatever is held right now must be released first
    baseline_ = std::move(active);
    have_baseline_ = true;
    return;
  }
  for (const InputBinding& candidate : active) {
    if (std::find(baseline_.begin(), baseline_.end(), candidate) != baseline_.end()) continue;
    if (candidate.device == InputDevice::Keyboard && candidate.code == Keys::kEscape) {
      cancel();
      return;
    }
    pending_ = candidate;
    conflicts_ = map_->find_conflicts(action_, pending_);
    if (conflicts_.empty()) {
      apply();
    } else {
      state_ = State::Conflict;
    }
    return;
  }
  baseline_ = std::move(active);  // released inputs may be pressed again
}

void InputRebinder::resolve(Resolution resolution) {
  if (state_ != State::Conflict) return;
  if (resolution == Resolution::Cancel) {
    cancel();
    return;
  }
  if (resolution == Resolution::Replace) {
    for (const std::string& name : conflicts_) {
      for (const InputAction& action : std::as_const(*map_).actions()) {
        if (action.name != name) continue;
        for (std::size_t i = action.bindings.size(); i-- > 0;) {
          const InputBinding& other = action.bindings[i];
          if (other.device == pending_.device && other.code == pending_.code &&
              (other.axis_sign == pending_.axis_sign || other.axis_sign == 0 || pending_.axis_sign == 0)) {
            map_->remove_binding(name, i);
          }
        }
        break;
      }
    }
  }
  apply();
}

void InputRebinder::apply() {
  if (slot_ == kAppend || !map_->replace_binding(action_, slot_, pending_)) map_->add_binding(action_, pending_);
  cancel();
}

namespace {
InputActionMap* g_active_input_map = nullptr;
}  // namespace

void set_active_input_map(InputActionMap* map) noexcept { g_active_input_map = map; }
InputActionMap* active_input_map() noexcept { return g_active_input_map; }

}  // namespace arcane
