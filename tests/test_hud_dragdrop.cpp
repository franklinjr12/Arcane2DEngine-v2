#include <vector>

#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

struct Rig {
  HudFocusGroup group;
  HudDragDrop dd;
  HudTooltip tip;
  FakeInputBackend in;
  std::vector<HudDropEvent> drops, rejects;

  Rig() {
    const auto cells = hud_layout_grid({0, 0, 400, 100}, 4, 4, 50, 50, 10, 0);
    const char* ids[] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; ++i) group.items.push_back({ids[i], cells[static_cast<std::size_t>(i)]});
    group.items[0].tooltip = "Slot A";
    group.items[1].tooltip = "Slot B";
    group.items[2].tooltip = "Slot C";
    group.items[3].enabled = false;
    group.items[3].disabled_reason = "locked";
    dd.accept = [](std::string_view, std::string_view t) {
      return t == "c" ? HudDropVerdict{false, "incompatible"} : HudDropVerdict{};
    };
    dd.on_drop = [this](const HudDropEvent& e) { drops.push_back(e); };
    dd.on_reject = [this](const HudDropEvent& e) { rejects.push_back(e); };
  }
  Vec2 center(int i) const {
    const HudRect& r = group.items[static_cast<std::size_t>(i)].rect;
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
  }
  void step() {
    group.update(in);
    dd.update(group, in);
  }
  void mouse_at(int i) { in.set_mouse_position(center(i)); }
  void press() { in.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true); step(); }
  void release() { in.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false); step(); }
  void tap(InputDevice d, int code) {
    in.set_down(d, code, true);
    step();
    in.set_down(d, code, false);
    step();
  }
  // Mouse drag source -> target; returns last events emitted.
  std::vector<HudDropEvent> mouse_drag(int from, int to) {
    std::vector<HudDropEvent> all;
    mouse_at(from);
    press();
    mouse_at(to);
    step();
    in.set_down(InputDevice::MouseButton, MouseButtons::kLeft, false);
    step();
    for (const auto& e : dd.events()) all.push_back(e);
    return all;
  }
};

bool same(const HudDropEvent& a, const HudDropEvent& b) {
  return a.source == b.source && a.target == b.target && a.accepted == b.accepted && a.reason == b.reason;
}

void test_mouse_valid_drop_once() {
  Rig r;
  const auto ev = r.mouse_drag(0, 1);
  ARCANE_ASSERT_TRUE(ev.size() == 1 && ev[0].accepted && ev[0].source == "a" && ev[0].target == "b");
  for (int i = 0; i < 5; ++i) r.step();
  ARCANE_ASSERT_TRUE(r.drops.size() == 1 && r.rejects.empty());
  ARCANE_ASSERT_TRUE(r.drops[0].source == "a" && r.drops[0].target == "b");
  ARCANE_ASSERT_TRUE(!r.dd.holding() && !r.dd.dragging());
}

void test_mouse_invalid_drops() {
  Rig r;
  const auto e1 = r.mouse_drag(0, 2);
  ARCANE_ASSERT_TRUE(e1.size() == 1 && !e1[0].accepted && e1[0].reason == "incompatible");
  const auto e2 = r.mouse_drag(0, 3);
  ARCANE_ASSERT_TRUE(e2.size() == 1 && !e2[0].accepted && e2[0].reason == "locked");
  ARCANE_ASSERT_TRUE(r.drops.empty() && r.rejects.size() == 2);
  // Picking up a disabled item rejects with its reason.
  r.mouse_at(3);
  r.press();
  ARCANE_ASSERT_TRUE(r.dd.events().size() == 1 && r.dd.events()[0].source == "d" &&
                     r.dd.events()[0].reason == "locked" && !r.dd.holding());
}

void test_keyboard_gamepad_match_mouse() {
  for (const int to : {1, 2, 3}) {
    Rig m;
    const auto mouse = m.mouse_drag(0, to);
    ARCANE_ASSERT_TRUE(mouse.size() == 1);
    for (const bool pad : {false, true}) {
      Rig r;
      const InputDevice dev = pad ? InputDevice::GamepadButton : InputDevice::Keyboard;
      const int pick = pad ? GamepadButtons::kA : Keys::kEnter;
      const int right = pad ? GamepadButtons::kDpadRight : 262;
      r.group.focused = 0;
      r.tap(dev, pick);
      ARCANE_ASSERT_TRUE(r.dd.holding() && *r.dd.held_id() == "a");
      // Focus skips disabled "d"; target it via direct focus instead.
      if (to == 3) r.group.focused = 3;
      else for (int i = 0; i < to; ++i) r.tap(dev, right);
      std::vector<HudDropEvent> got;
      r.in.set_down(dev, pick, true);
      r.step();
      got = r.dd.events();
      r.in.set_down(dev, pick, false);
      r.step();
      ARCANE_ASSERT_TRUE(got.size() == 1 && same(got[0], mouse[0]));
      ARCANE_ASSERT_TRUE(r.drops.size() + r.rejects.size() == 1 && !r.dd.holding());
    }
  }
}

void test_cancel_empty_and_click_click() {
  Rig r;
  r.group.focused = 0;
  r.tap(InputDevice::Keyboard, Keys::kEnter);
  ARCANE_ASSERT_TRUE(r.dd.holding());
  r.tap(InputDevice::Keyboard, 256);
  ARCANE_ASSERT_TRUE(!r.dd.holding() && r.drops.empty() && r.rejects.empty());
  r.tap(InputDevice::GamepadButton, GamepadButtons::kA);
  r.tap(InputDevice::GamepadButton, GamepadButtons::kB);
  ARCANE_ASSERT_TRUE(!r.dd.holding() && r.drops.empty() && r.rejects.empty());

  r.mouse_at(0);
  r.press();
  r.in.set_mouse_position({390.0f, 90.0f});  // empty space
  r.release();
  ARCANE_ASSERT_TRUE(!r.dd.holding() && r.drops.empty() && r.rejects.empty());

  // click on a, release on a (holding), click b.
  r.mouse_at(0);
  r.press();
  r.release();
  ARCANE_ASSERT_TRUE(r.dd.holding() && !r.dd.dragging());
  r.mouse_at(1);
  r.press();
  r.release();
  ARCANE_ASSERT_TRUE(r.drops.size() == 1 && r.drops[0].source == "a" && r.drops[0].target == "b");
  ARCANE_ASSERT_TRUE(!r.dd.holding());
}

void test_tooltip() {
  Rig r;
  r.in.set_mouse_position({-100.0f, -100.0f});
  r.group.focused = 0;
  r.tip.delay = 0.5f;
  for (int i = 0; i < 3; ++i) {
    r.tip.update(r.group, r.in, 0.1f);
    ARCANE_ASSERT_TRUE(!r.tip.visible());
  }
  r.tip.update(r.group, r.in, 0.1f);
  r.tip.update(r.group, r.in, 0.1f);
  ARCANE_ASSERT_TRUE(r.tip.visible() && r.tip.text() == "Slot A" && *r.tip.target_id() == "a");
  const HudRect a = r.group.items[0].rect;
  ARCANE_ASSERT_TRUE(r.tip.rect().x == a.x && r.tip.rect().y == a.y + a.h + r.tip.offset_y);
  // Follows focus immediately.
  r.group.focused = 1;
  r.tip.update(r.group, r.in, 0.0f);
  ARCANE_ASSERT_TRUE(r.tip.visible() && r.tip.text() == "Slot B" && *r.tip.target_id() == "b");
  ARCANE_ASSERT_TRUE(r.tip.rect().x == r.group.items[1].rect.x);
  // Disabled item shows the reason.
  r.group.focused = 3;
  r.tip.update(r.group, r.in, 0.0f);
  ARCANE_ASSERT_TRUE(r.tip.text() == "locked" && *r.tip.target_id() == "d");
  // Pointer hover wins over focus.
  r.mouse_at(2);
  r.tip.update(r.group, r.in, 0.0f);
  ARCANE_ASSERT_TRUE(r.tip.text() == "Slot C" && *r.tip.target_id() == "c");
  // No target: hides and the delay restarts.
  r.in.set_mouse_position({-100.0f, -100.0f});
  r.group.focused = -1;
  r.tip.update(r.group, r.in, 0.1f);
  ARCANE_ASSERT_TRUE(!r.tip.visible() && r.tip.target_id() == nullptr);
  r.group.focused = 0;
  r.tip.update(r.group, r.in, 0.3f);
  ARCANE_ASSERT_TRUE(!r.tip.visible());
  r.tip.update(r.group, r.in, 0.3f);
  ARCANE_ASSERT_TRUE(r.tip.visible());
}

void test_gameplay_mask() {
  Rig r;
  r.mouse_at(0);
  r.in.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  r.step();
  ARCANE_ASSERT_TRUE(r.dd.dragging());
  ARCANE_ASSERT_TRUE(!r.dd.gameplay_backend().is_down(InputDevice::MouseButton, MouseButtons::kLeft));
  r.mouse_at(1);
  r.step();
  ARCANE_ASSERT_TRUE(!r.dd.gameplay_backend().is_down(InputDevice::MouseButton, MouseButtons::kLeft));
  r.release();
  ARCANE_ASSERT_TRUE(!r.dd.dragging());
  // Click on empty space is not masked.
  Rig q;
  q.in.set_mouse_position({390.0f, 90.0f});
  q.in.set_down(InputDevice::MouseButton, MouseButtons::kLeft, true);
  q.step();
  ARCANE_ASSERT_TRUE(q.dd.gameplay_backend().is_down(InputDevice::MouseButton, MouseButtons::kLeft));
}

}  // namespace

int main() {
  test_mouse_valid_drop_once();
  test_mouse_invalid_drops();
  test_keyboard_gamepad_match_mouse();
  test_cancel_empty_and_click_click();
  test_tooltip();
  test_gameplay_mask();
  return 0;
}
