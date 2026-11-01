#include <cmath>

#include "arcane/hud.hpp"
#include "test_assert.hpp"

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void test_scale_follows_screen_height() {
  ARCANE_ASSERT_TRUE(near(arcane::hud_scale(720), 1.0f));
  ARCANE_ASSERT_TRUE(near(arcane::hud_scale(1440), 2.0f));
}

void test_anchors_place_corners_and_center() {
  using arcane::HudAnchor;
  // 1280x720 => scale 1.
  auto tl = arcane::hud_layout(HudAnchor::TopLeft, 10, 20, 100, 50, 1280, 720);
  ARCANE_ASSERT_TRUE(near(tl.x, 10) && near(tl.y, 20) && near(tl.w, 100) && near(tl.h, 50));
  auto br = arcane::hud_layout(HudAnchor::BottomRight, -10, -20, 100, 50, 1280, 720);
  ARCANE_ASSERT_TRUE(near(br.x, 1280 - 100 - 10) && near(br.y, 720 - 50 - 20));
  auto c = arcane::hud_layout(HudAnchor::Center, 0, 0, 100, 50, 1280, 720);
  ARCANE_ASSERT_TRUE(near(c.x, 590) && near(c.y, 335));
  auto top = arcane::hud_layout(HudAnchor::Top, 0, 5, 200, 10, 1280, 720);
  ARCANE_ASSERT_TRUE(near(top.x, 540) && near(top.y, 5));
  auto left = arcane::hud_layout(HudAnchor::Left, 0, 0, 10, 100, 1280, 720);
  ARCANE_ASSERT_TRUE(near(left.x, 0) && near(left.y, 310));
}

void test_layout_scales_with_resolution() {
  auto r = arcane::hud_layout(arcane::HudAnchor::TopLeft, 10, 10, 100, 50, 2560, 1440);
  ARCANE_ASSERT_TRUE(near(r.x, 20) && near(r.y, 20) && near(r.w, 200) && near(r.h, 100));
}

void test_text_size_scales_and_grows_with_length() {
  float w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
  arcane::hud_text_size("HP", 1.0f, w1, h1);
  arcane::hud_text_size("HP", 2.0f, w2, h2);
  arcane::hud_text_size("HP 100", 1.0f, w3, h3);
  ARCANE_ASSERT_TRUE(w1 > 0 && h1 > 0);
  ARCANE_ASSERT_TRUE(near(w2, w1 * 2) && near(h2, h1 * 2));
  ARCANE_ASSERT_TRUE(w3 > w1);
}

void test_hud_keeps_insertion_order() {
  arcane::Hud hud;
  hud.add(arcane::HudText{.text = "a"});
  hud.add(arcane::HudProgressBar{});
  ARCANE_ASSERT_TRUE(hud.elements().size() == 2);
  ARCANE_ASSERT_TRUE(std::holds_alternative<arcane::HudText>(hud.elements()[0]));
  ARCANE_ASSERT_TRUE(std::holds_alternative<arcane::HudProgressBar>(hud.elements()[1]));
  hud.clear();
  ARCANE_ASSERT_TRUE(hud.elements().empty());
}

void test_remove_and_contains() {
  arcane::Hud hud;
  hud.add(arcane::HudText{.id = "t", .text = "a"});
  hud.add(arcane::HudProgressBar{.id = "bar"});
  arcane::HudButton button;
  button.id = "btn";
  hud.add(button);
  ARCANE_ASSERT_TRUE(hud.contains("t") && hud.contains("bar") && hud.contains("btn"));
  ARCANE_ASSERT_TRUE(!hud.contains("nope"));
  ARCANE_ASSERT_TRUE(hud.remove("bar"));  // any kind
  ARCANE_ASSERT_TRUE(!hud.contains("bar") && hud.elements().size() == 2);
  ARCANE_ASSERT_TRUE(!hud.remove("bar"));  // already gone
  ARCANE_ASSERT_TRUE(hud.remove("btn") && hud.remove("t"));
  ARCANE_ASSERT_TRUE(hud.elements().empty());
}

}  // namespace

int main() {
  test_remove_and_contains();
  test_scale_follows_screen_height();
  test_anchors_place_corners_and_center();
  test_layout_scales_with_resolution();
  test_text_size_scales_and_grows_with_length();
  test_hud_keeps_insertion_order();
  return 0;
}
