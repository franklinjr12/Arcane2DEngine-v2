#include "arcane/input.hpp"

#include "test_assert.hpp"

#include <cmath>

namespace {

// held() reflects current state; pressed()/released() only fire on the
// frame the state actually changed (edge detection), and two bindings on
// the same action OR together.
void test_action_edge_detection_and_multiple_bindings() {
  arcane::InputActionMap map;
  map.add_binding("jump", arcane::InputBinding{arcane::InputDevice::Keyboard, arcane::Keys::kSpace});
  map.add_binding("jump", arcane::InputBinding{arcane::InputDevice::GamepadButton, arcane::GamepadButtons::kA});

  arcane::FakeInputBackend backend;
  map.update(backend);
  ARCANE_ASSERT_TRUE(!map.held("jump"));
  ARCANE_ASSERT_TRUE(!map.pressed("jump"));
  ARCANE_ASSERT_TRUE(!map.released("jump"));

  backend.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kSpace, true);
  map.update(backend);
  ARCANE_ASSERT_TRUE(map.held("jump"));
  ARCANE_ASSERT_TRUE(map.pressed("jump"));
  ARCANE_ASSERT_TRUE(!map.released("jump"));

  // Still held on the next frame: no longer "pressed".
  map.update(backend);
  ARCANE_ASSERT_TRUE(map.held("jump"));
  ARCANE_ASSERT_TRUE(!map.pressed("jump"));

  // Swap to the other binding without releasing the first: held stays true
  // throughout, so no spurious pressed/released at the OR boundary.
  backend.set_down(arcane::InputDevice::GamepadButton, arcane::GamepadButtons::kA, true);
  backend.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kSpace, false);
  map.update(backend);
  ARCANE_ASSERT_TRUE(map.held("jump"));
  ARCANE_ASSERT_TRUE(!map.pressed("jump"));
  ARCANE_ASSERT_TRUE(!map.released("jump"));

  backend.set_down(arcane::InputDevice::GamepadButton, arcane::GamepadButtons::kA, false);
  map.update(backend);
  ARCANE_ASSERT_TRUE(!map.held("jump"));
  ARCANE_ASSERT_TRUE(map.released("jump"));

  // An unknown action name reads as "not pressed", not an error.
  ARCANE_ASSERT_TRUE(!map.held("does_not_exist"));
}

void test_clear_bindings() {
  arcane::InputActionMap map;
  map.add_binding("fire", arcane::InputBinding{arcane::InputDevice::MouseButton, arcane::MouseButtons::kLeft});
  map.clear_bindings("fire");

  arcane::FakeInputBackend backend;
  backend.set_down(arcane::InputDevice::MouseButton, arcane::MouseButtons::kLeft, true);
  map.update(backend);
  ARCANE_ASSERT_TRUE(!map.held("fire"));
}

// Only the action/binding config round-trips through JSON; per-frame state
// does not (a freshly loaded map reads every action as "not held").
void test_json_round_trip() {
  arcane::InputActionMap original;
  original.add_binding("move_left", arcane::InputBinding{arcane::InputDevice::Keyboard, arcane::Keys::kA});
  original.add_binding("move_right", arcane::InputBinding{arcane::InputDevice::Keyboard, arcane::Keys::kD});
  original.add_binding("move_right", arcane::InputBinding{arcane::InputDevice::GamepadButton, arcane::GamepadButtons::kDpadRight});

  const std::string json = original.save_to_json();
  auto loaded = arcane::InputActionMap::load_from_json(json);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->actions().size() == 2);

  arcane::FakeInputBackend backend;
  backend.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kA, true);
  loaded->update(backend);
  ARCANE_ASSERT_TRUE(loaded->held("move_left"));
  ARCANE_ASSERT_TRUE(!loaded->held("move_right"));

  backend.set_down(arcane::InputDevice::GamepadButton, arcane::GamepadButtons::kDpadRight, true);
  loaded->update(backend);
  ARCANE_ASSERT_TRUE(loaded->held("move_right"));

  auto bad_version = arcane::InputActionMap::load_from_json(R"({"format_version":2,"actions":[]})");
  ARCANE_ASSERT_TRUE(!bad_version.has_value());
}

void test_active_input_map_is_null_by_default() {
  ARCANE_ASSERT_TRUE(arcane::active_input_map() == nullptr);
  arcane::InputActionMap map;
  arcane::set_active_input_map(&map);
  ARCANE_ASSERT_TRUE(arcane::active_input_map() == &map);
  arcane::set_active_input_map(nullptr);
  ARCANE_ASSERT_TRUE(arcane::active_input_map() == nullptr);
}

void test_axes_and_players() {
  using namespace arcane;
  ARCANE_ASSERT_TRUE(apply_deadzone(0.1f, 0.2f) == 0.0f);
  ARCANE_ASSERT_TRUE(std::fabs(apply_deadzone(-0.6f, 0.2f) + 0.5f) < 0.0001f);
  InputActionMap map;
  InputBinding axis{InputDevice::GamepadAxis, GamepadAxes::kLeftX};
  axis.axis_sign = 1; axis.deadzone = 0.2f; axis.threshold = 0.6f;
  map.set_action({"move", {axis}, 1});
  map.set_action({"fire", {{InputDevice::GamepadButton, GamepadButtons::kA}}, 2});
  FakeInputBackend backend;
  backend.set_gamepad_axis(0, GamepadAxes::kLeftX, 1.0f);
  backend.set_gamepad_button(1, GamepadButtons::kA, true);
  map.update(backend);
  ARCANE_ASSERT_TRUE(map.value("move") == 0.0f && !map.held("fire"));
  backend.set_gamepad_axis(1, GamepadAxes::kLeftX, 0.6f);
  backend.set_gamepad_button(2, GamepadButtons::kA, true);
  map.update(backend);
  ARCANE_ASSERT_TRUE(std::fabs(map.value("move") - 0.5f) < 0.0001f);
  ARCANE_ASSERT_TRUE(!map.held("move") && map.pressed("fire"));
  backend.set_gamepad_axis(1, GamepadAxes::kLeftX, 1.0f);
  map.update(backend);
  ARCANE_ASSERT_TRUE(map.pressed("move") && map.axis("missing", "move") == 1.0f);
  auto loaded = InputActionMap::load_from_json(map.save_to_json());
  ARCANE_ASSERT_TRUE(loaded && loaded->actions()[0].player == 1 && loaded->actions()[0].bindings[0] == axis);
  auto old = InputActionMap::load_from_json(R"({"format_version":1,"actions":[{"name":"old","bindings":[{"device":"gamepad_button","code":0}]}]})");
  ARCANE_ASSERT_TRUE(old && old->actions()[0].player == -1 && old->actions()[0].bindings[0].pad == -1);
}

void test_rebinding() {
  using namespace arcane;
  InputActionMap map;
  map.add_binding("jump", {InputDevice::Keyboard, Keys::kSpace});
  map.add_binding("fire", {InputDevice::Keyboard, Keys::kA});
  FakeInputBackend backend;
  InputRebinder rebinder;
  backend.set_down(InputDevice::Keyboard, Keys::kSpace, true);
  rebinder.begin(map, "jump", 0);
  rebinder.update(backend);
  rebinder.update(backend);
  ARCANE_ASSERT_TRUE(rebinder.state() == InputRebinder::State::Listening);
  backend.set_down(InputDevice::Keyboard, Keys::kSpace, false);
  rebinder.update(backend);
  backend.set_down(InputDevice::Keyboard, Keys::kA, true);
  rebinder.update(backend);
  ARCANE_ASSERT_TRUE(rebinder.state() == InputRebinder::State::Conflict && rebinder.conflicts().size() == 1);
  rebinder.resolve(InputRebinder::Resolution::Replace);
  ARCANE_ASSERT_TRUE(rebinder.state() == InputRebinder::State::Idle);
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings[0].code == Keys::kA && map.actions()[1].bindings.empty());
}

}  // namespace

int main() {
  test_action_edge_detection_and_multiple_bindings();
  test_clear_bindings();
  test_json_round_trip();
  test_active_input_map_is_null_by_default();
  test_axes_and_players();
  test_rebinding();
  return 0;
}
