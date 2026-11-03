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

void test_rebind_api_and_reset() {
  using namespace arcane;
  InputActionMap map;
  map.add_binding("jump", {InputDevice::Keyboard, Keys::kSpace});
  map.add_binding("jump", {InputDevice::GamepadButton, GamepadButtons::kA});
  map.add_binding("fire", {InputDevice::Keyboard, Keys::kA});
  map.capture_defaults();

  // Conflict is reported, not silently overwritten.
  auto conflicts = map.rebind("jump", {InputDevice::Keyboard, Keys::kA});
  ARCANE_ASSERT_TRUE(conflicts.size() == 1 && conflicts[0] == "fire");
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings.size() == 2 && map.actions()[0].bindings[0].code == Keys::kSpace);
  ARCANE_ASSERT_TRUE(map.actions()[1].bindings[0].code == Keys::kA);

  // Replace per device: keyboard changes, gamepad binding stays.
  ARCANE_ASSERT_TRUE(map.rebind("jump", {InputDevice::Keyboard, Keys::kW}).empty());
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings.size() == 2);
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings[0].code == Keys::kW);
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings[1].device == InputDevice::GamepadButton);

  // Explicit allow keeps both actions on the key.
  ARCANE_ASSERT_TRUE(map.rebind("jump", {InputDevice::Keyboard, Keys::kA}, true).empty());
  ARCANE_ASSERT_TRUE(map.find_conflicts("jump", {InputDevice::Keyboard, Keys::kA}).size() == 1);

  map.clear_bindings("jump", InputDevice::Keyboard);
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings.size() == 1);
  ARCANE_ASSERT_TRUE(map.rebind("new_action", {InputDevice::Keyboard, Keys::kD}).empty());

  ARCANE_ASSERT_TRUE(map.reset_to_defaults("jump"));
  ARCANE_ASSERT_TRUE(map.actions()[0].bindings.size() == 2 && map.actions()[0].bindings[0].code == Keys::kSpace);
  ARCANE_ASSERT_TRUE(!map.reset_to_defaults("nope"));
  ARCANE_ASSERT_TRUE(map.reset_to_defaults());
  ARCANE_ASSERT_TRUE(map.actions().size() == 2 && map.actions()[1].bindings[0].code == Keys::kA);

  InputActionMap fresh;
  ARCANE_ASSERT_TRUE(!fresh.reset_to_defaults());
}

arcane::InputRecording run_script(arcane::InputActionMap& map, int ticks) {
  using namespace arcane;
  FakeInputBackend backend;
  InputRecording rec;
  std::uint32_t seed = 12345;
  for (int t = 0; t < ticks; ++t) {
    seed = seed * 1664525u + 1013904223u;
    backend.set_down(InputDevice::Keyboard, Keys::kSpace, (seed >> 24) & 1);
    backend.set_down(InputDevice::Keyboard, Keys::kD, (seed >> 25) & 1);
    backend.set_gamepad_axis(0, GamepadAxes::kLeftX, static_cast<float>((seed >> 16) & 0xff) / 127.5f - 1.0f);
    map.update(backend);
    rec.record(map);
  }
  return rec;
}

void test_record_replay() {
  using namespace arcane;
  auto make = [] {
    InputActionMap m;
    m.add_binding("jump", {InputDevice::Keyboard, Keys::kSpace});
    m.add_binding("right", {InputDevice::Keyboard, Keys::kD});
    m.add_binding("right", {InputDevice::GamepadAxis, GamepadAxes::kLeftX, -1, 1});
    return m;
  };
  InputActionMap a = make();
  InputActionMap b = make();
  const InputRecording first = run_script(a, 600);
  const InputRecording second = run_script(b, 600);
  ARCANE_ASSERT_TRUE(first.ticks() == 600 && first.bytes().size() > 0);
  ARCANE_ASSERT_TRUE(first.bytes() == second.bytes());

  // Playing the recording back reproduces every tick's action states.
  InputActionMap c = make();
  InputRecording replayed;
  bool any_pressed = false;
  for (std::size_t t = 0; t < first.ticks(); ++t) {
    ARCANE_ASSERT_TRUE(first.play(c, t));
    any_pressed = any_pressed || c.pressed("jump");
    replayed.record(c);
  }
  ARCANE_ASSERT_TRUE(any_pressed);
  ARCANE_ASSERT_TRUE(replayed.bytes() == first.bytes());
  ARCANE_ASSERT_TRUE(!first.play(c, 600));

  // A different binding set yields a different recording.
  InputActionMap d = make();
  d.rebind("jump", {InputDevice::Keyboard, Keys::kW});
  ARCANE_ASSERT_TRUE(run_script(d, 600).bytes() != first.bytes());
}

}  // namespace

int main() {
  test_action_edge_detection_and_multiple_bindings();
  test_clear_bindings();
  test_json_round_trip();
  test_active_input_map_is_null_by_default();
  test_axes_and_players();
  test_rebinding();
  test_rebind_api_and_reset();
  test_record_replay();
  return 0;
}
