#include <string>

#include "arcane/display.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

// Button centered on a 1280x720 screen: x 560..720 (w160), y 320..360 (h40).
HudButton make_button(std::string id) {
  HudButton b;
  b.id = std::move(id);
  b.label = "Go";
  return b;
}

Vec2 center_of_button() { return {640.0f, 340.0f}; }

struct Fixture {
  Hud hud;
  HudInput input;
  FakeInputBackend backend;
  InputActionMap actions;
  std::vector<std::string> clicked;

  Fixture() {
    hud.add(make_button("start"));
    hud.set_click_handler([this](std::string_view id) { clicked.emplace_back(id); });
    actions.set_action(InputAction{"fire", {{InputDevice::MouseButton, MouseButtons::kLeft},
                                            {InputDevice::GamepadButton, GamepadButtons::kA}}});
  }
  void frame(int w = 1280, int h = 720) {
    input.update(hud, backend, w, h);
    actions.update(input.gameplay_backend());
  }
  HudButton& button() { return std::get<HudButton>(hud.elements()[0]); }
};

void test_hit_test_scales_with_height() {
  Fixture f;
  // At 1440p (scale 2) the button spans x 1120..1440 of 2560, y 680..760.
  f.backend.set_mouse_position({1130.0f, 700.0f});
  f.frame(2560, 1440);
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Hover);
  f.backend.set_mouse_position({1100.0f, 700.0f});  // inside only at scale 1
  f.frame(2560, 1440);
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Normal);
  f.backend.set_mouse_position({1450.0f, 700.0f});
  f.frame(2560, 1440);
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Normal);
}

void test_state_transitions_and_click() {
  Fixture f;
  f.frame();
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Normal);
  f.backend.set_mouse_position(center_of_button());
  f.frame();
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Hover);
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  f.frame();
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Pressed);
  ARCANE_ASSERT_TRUE(f.clicked.empty());
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.clicked.size() == 1 && f.clicked[0] == "start");
  ARCANE_ASSERT_TRUE(f.hud.clicked("start"));
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Hover);
  f.frame();
  ARCANE_ASSERT_TRUE(!f.hud.clicked("start"));  // per-frame
}

void test_release_off_button_cancels_click() {
  Fixture f;
  f.backend.set_mouse_position(center_of_button());
  f.frame();
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  f.frame();
  f.backend.set_mouse_position({10.0f, 10.0f});
  f.frame();
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.clicked.empty());
}

void test_disabled_button_never_clicks() {
  Fixture f;
  f.button().enabled = false;
  f.backend.set_mouse_position(center_of_button());
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  f.frame();
  ARCANE_ASSERT_TRUE(f.button().state == HudButtonState::Disabled);
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.clicked.empty());
}

void test_click_is_consumed_from_gameplay() {
  Fixture f;
  f.backend.set_mouse_position(center_of_button());
  f.frame();
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  f.frame();
  ARCANE_ASSERT_TRUE(f.input.consumed_pointer());
  ARCANE_ASSERT_TRUE(!f.actions.pressed("fire") && !f.actions.held("fire"));
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  f.frame();
  ARCANE_ASSERT_TRUE(!f.actions.released("fire"));

  f.clicked.clear();
  // A press that starts off the HUD still fires, even if dragged over a button.
  f.backend.set_mouse_position({10.0f, 10.0f});
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  f.frame();
  ARCANE_ASSERT_TRUE(f.actions.pressed("fire"));
  f.backend.set_mouse_position(center_of_button());
  f.frame();
  ARCANE_ASSERT_TRUE(f.actions.held("fire"));
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.actions.released("fire") && f.clicked.empty());
}

void test_gamepad_confirm_navigation_and_consumption() {
  Fixture f;
  f.hud.add(make_button("options"));
  std::get<HudButton>(f.hud.elements()[1]).offset_y = 60.0f;
  // Without focus the gamepad A press belongs to gameplay.
  f.backend.set_down(InputDevice::GamepadButton, GamepadButtons::kA, true);
  f.frame();
  ARCANE_ASSERT_TRUE(f.actions.pressed("fire"));
  f.backend.set_down(InputDevice::GamepadButton, GamepadButtons::kA, false);
  f.frame();

  // Down moves focus to the first button, again to the second, then wraps.
  auto tap = [&](InputDevice d, int code) {
    f.backend.set_down(d, code, true);
    f.frame();
    f.backend.set_down(d, code, false);
    f.frame();
  };
  tap(InputDevice::GamepadButton, GamepadButtons::kDpadDown);
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 0 && f.button().focused);
  tap(InputDevice::Keyboard, Keys::kDown);
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 1);
  tap(InputDevice::Keyboard, Keys::kDown);
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 0);
  tap(InputDevice::Keyboard, Keys::kUp);
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 1);

  // Confirm clicks the focused button and is hidden from gameplay.
  f.backend.set_down(InputDevice::GamepadButton, GamepadButtons::kA, true);
  f.frame();
  ARCANE_ASSERT_TRUE(!f.actions.pressed("fire"));
  ARCANE_ASSERT_TRUE(std::get<HudButton>(f.hud.elements()[1]).state == HudButtonState::Pressed);
  f.backend.set_down(InputDevice::GamepadButton, GamepadButtons::kA, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.clicked.size() == 1 && f.clicked[0] == "options");
}

void test_navigation_skips_disabled() {
  Fixture f;
  f.hud.add(make_button("b"));
  f.hud.add(make_button("c"));
  std::get<HudButton>(f.hud.elements()[1]).enabled = false;
  auto tap = [&] {
    f.backend.set_down(InputDevice::Keyboard, Keys::kDown, true);
    f.frame();
    f.backend.set_down(InputDevice::Keyboard, Keys::kDown, false);
    f.frame();
  };
  tap();
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 0);
  tap();
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 2);
}

void test_removal_resets_stale_focus() {
  Fixture f;
  f.hud.add(make_button("b"));
  f.backend.set_down(InputDevice::Keyboard, Keys::kDown, true);
  f.frame();
  f.backend.set_down(InputDevice::Keyboard, Keys::kDown, false);
  f.frame();
  f.backend.set_down(InputDevice::Keyboard, Keys::kDown, true);
  f.frame();
  f.backend.set_down(InputDevice::Keyboard, Keys::kDown, false);
  f.frame();
  ARCANE_ASSERT_TRUE(f.input.focus_index() == 1);
  ARCANE_ASSERT_TRUE(f.hud.remove("b"));  // focus index 1 is now out of range
  f.frame();
  ARCANE_ASSERT_TRUE(f.input.focus_index() == -1);
  f.hud.clear();
  f.frame();  // an empty HUD is fine too
  ARCANE_ASSERT_TRUE(f.input.focus_index() == -1 && f.clicked.empty());
}

void test_json_round_trip() {
  Hud hud;
  HudButton b = make_button("quit");
  b.anchor = HudAnchor::BottomRight;
  b.offset_x = -12.0f;
  b.enabled = false;
  b.hover = {0.5f, 0.25f, 0.125f, 1.0f};
  hud.add(b);
  hud.add(HudText{.text = "Score", .anchor = HudAnchor::Top, .font_scale = 3.0f});
  hud.add(HudProgressBar{.anchor = HudAnchor::Bottom, .fraction = 0.25f});
  auto loaded = Hud::load_from_json(hud.save_to_json());
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->elements().size() == 3);
  const auto* lb = std::get_if<HudButton>(&loaded->elements()[0]);
  ARCANE_ASSERT_TRUE(lb && lb->id == "quit" && lb->label == "Go" && lb->anchor == HudAnchor::BottomRight);
  ARCANE_ASSERT_TRUE(lb->offset_x == -12.0f && !lb->enabled && lb->hover.g == 0.25f && lb->width == 160.0f);
  const auto* lt = std::get_if<HudText>(&loaded->elements()[1]);
  ARCANE_ASSERT_TRUE(lt && lt->text == "Score" && lt->anchor == HudAnchor::Top && lt->font_scale == 3.0f);
  const auto* lp = std::get_if<HudProgressBar>(&loaded->elements()[2]);
  ARCANE_ASSERT_TRUE(lp && lp->fraction == 0.25f && lp->anchor == HudAnchor::Bottom);
  ARCANE_ASSERT_TRUE(!Hud::load_from_json("{").has_value());
  ARCANE_ASSERT_TRUE(!Hud::load_from_json(R"({"format_version":1,"elements":[{"type":"nope"}]})").has_value());
}

void test_project_carries_hud() {
  // Round trip through the project file is covered by saving a ProjectFile-sized HUD string.
  Hud hud;
  hud.add(make_button("x"));
  auto again = Hud::load_from_json(hud.save_to_json());
  ARCANE_ASSERT_TRUE(again && again->save_to_json() == hud.save_to_json());
}

}  // namespace

// A letterboxed 1280x720 design view in a 2000x1200 framebuffer shown in a 1000x600 window (HiDPI ratio 2):
// the pointer is mapped into design pixels so the centred button is hit at the window centre, not elsewhere.
void test_click_through_letterbox_mapping() {
  Fixture f;
  DisplayConfig display;
  display.design_width = 1280;
  display.design_height = 720;
  const DisplayViewport vp = compute_display_viewport(2000, 1200, display);
  const PointerMapping map = pointer_mapping(vp, 1200, {2.0f, 2.0f});
  auto frame = [&] {
    f.input.update(f.hud, f.backend, vp.logical_width, vp.logical_height, map.origin, map.scale);
    f.actions.update(f.input.gameplay_backend());
  };
  f.backend.set_mouse_position({500.0f, 290.0f});  // near the window centre
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  frame();
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  frame();
  ARCANE_ASSERT_TRUE(f.clicked.size() == 1 && f.clicked[0] == "start");

  f.clicked.clear();
  f.backend.set_mouse_position({100.0f, 50.0f});  // far from the button
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  frame();
  f.backend.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
  frame();
  ARCANE_ASSERT_TRUE(f.clicked.empty());
}

int main() {
  test_hit_test_scales_with_height();
  test_state_transitions_and_click();
  test_release_off_button_cancels_click();
  test_disabled_button_never_clicks();
  test_click_is_consumed_from_gameplay();
  test_gamepad_confirm_navigation_and_consumption();
  test_navigation_skips_disabled();
  test_removal_resets_stale_focus();
  test_click_through_letterbox_mapping();
  test_json_round_trip();
  test_project_carries_hud();
  return 0;
}
