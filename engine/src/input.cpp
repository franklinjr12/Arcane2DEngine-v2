#include "arcane/input.hpp"

#include <GLFW/glfw3.h>

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
  }
  return "keyboard";
}

std::expected<InputDevice, std::string> device_from_string(const std::string& text) {
  if (text == "keyboard") return InputDevice::Keyboard;
  if (text == "mouse_button") return InputDevice::MouseButton;
  if (text == "gamepad_button") return InputDevice::GamepadButton;
  return std::unexpected(std::format("unknown input device '{}'", text));
}

// Packs device+code into one map key; collisions are impossible since code
// is restricted to small non-negative values in practice (GLFW codes) and
// device only ever takes 3 values.
std::uint64_t pack(InputDevice device, int code) noexcept {
  return (static_cast<std::uint64_t>(static_cast<int>(device)) << 32) | static_cast<std::uint32_t>(code);
}
}  // namespace

bool GlfwInputBackend::is_down(InputDevice device, int code) const {
  auto* handle = static_cast<GLFWwindow*>(window_->native_handle());
  switch (device) {
    case InputDevice::Keyboard:
      return glfwGetKey(handle, code) == GLFW_PRESS;
    case InputDevice::MouseButton:
      return glfwGetMouseButton(handle, code) == GLFW_PRESS;
    case InputDevice::GamepadButton: {
      GLFWgamepadstate state;
      if (!glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) return false;
      if (code < 0 || code > GLFW_GAMEPAD_BUTTON_LAST) return false;
      return state.buttons[code] == GLFW_PRESS;
    }
  }
  return false;
}

Vec2 GlfwInputBackend::mouse_position() const {
  auto* handle = static_cast<GLFWwindow*>(window_->native_handle());
  double x = 0.0, y = 0.0;
  glfwGetCursorPos(handle, &x, &y);
  return {static_cast<float>(x), static_cast<float>(y)};
}

void FakeInputBackend::set_down(InputDevice device, int code, bool down) { down_[pack(device, code)] = down; }

bool FakeInputBackend::is_down(InputDevice device, int code) const {
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
    for (const auto& binding : action.bindings) {
      if (backend.is_down(binding.device, binding.code)) {
        down = true;
        break;
      }
    }
    auto& state = state_[action.name];
    state.down_previous = state.down;
    state.down = down;
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

std::string InputActionMap::save_to_json() const {
  nlohmann::json root;
  root["format_version"] = kInputFormatVersion;
  nlohmann::json actions_json = nlohmann::json::array();
  for (const auto& action : actions_) {
    nlohmann::json action_json;
    action_json["name"] = action.name;
    nlohmann::json bindings_json = nlohmann::json::array();
    for (const auto& binding : action.bindings) {
      bindings_json.push_back({{"device", device_to_string(binding.device)}, {"code", binding.code}});
    }
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
      action.bindings.push_back(InputBinding{*device, binding_json.value("code", 0)});
    }
    map.set_action(std::move(action));
  }
  return map;
}

namespace {
InputActionMap* g_active_input_map = nullptr;
}  // namespace

void set_active_input_map(InputActionMap* map) noexcept { g_active_input_map = map; }
InputActionMap* active_input_map() noexcept { return g_active_input_map; }

}  // namespace arcane
