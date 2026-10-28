#include "arcane/input.hpp"

#include "test_assert.hpp"

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

}  // namespace

int main() {
  test_action_edge_detection_and_multiple_bindings();
  test_clear_bindings();
  test_json_round_trip();
  test_active_input_map_is_null_by_default();
  return 0;
}
