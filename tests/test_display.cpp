#include <cmath>

#include "arcane/display.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

DisplayConfig design(int w, int h, DisplayScaleMode mode = DisplayScaleMode::Letterbox) { return {w, h, mode}; }

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void test_no_design_passthrough() {
  const DisplayViewport vp = compute_display_viewport(1000, 700, {});
  ARCANE_ASSERT_TRUE(vp.x == 0 && vp.y == 0 && vp.width == 1000 && vp.height == 700);
  ARCANE_ASSERT_TRUE(vp.logical_width == 1000 && vp.logical_height == 700);
  ARCANE_ASSERT_TRUE(vp.scale_x == 1.0f && vp.scale_y == 1.0f);
  // Stretch without a design size is also a passthrough.
  ARCANE_ASSERT_TRUE(compute_display_viewport(1000, 700, design(0, 0, DisplayScaleMode::Stretch)) == vp);
}

void test_letterbox_wide_window() {
  // 640x360 design in a 1600x900 window: exact 2.5x fit.
  DisplayViewport vp = compute_display_viewport(1600, 900, design(640, 360));
  ARCANE_ASSERT_TRUE(vp.x == 0 && vp.y == 0 && vp.width == 1600 && vp.height == 900);
  // Wider window: bars left and right.
  vp = compute_display_viewport(2000, 900, design(640, 360));
  ARCANE_ASSERT_TRUE(vp.width == 1600 && vp.height == 900);
  ARCANE_ASSERT_TRUE(vp.x == 200 && vp.y == 0);
  ARCANE_ASSERT_TRUE(vp.logical_width == 640 && vp.logical_height == 360);
  ARCANE_ASSERT_TRUE(near(vp.scale_x, 2.5f) && near(vp.scale_y, 2.5f));
}

void test_letterbox_tall_window() {
  // Tall window: bars top and bottom.
  const DisplayViewport vp = compute_display_viewport(640, 600, design(640, 360));
  ARCANE_ASSERT_TRUE(vp.width == 640 && vp.height == 360);
  ARCANE_ASSERT_TRUE(vp.x == 0 && vp.y == 120);
  ARCANE_ASSERT_TRUE(near(vp.scale_x, 1.0f) && near(vp.scale_y, 1.0f));
}

void test_letterbox_exact_and_small() {
  const DisplayViewport exact = compute_display_viewport(640, 360, design(640, 360));
  ARCANE_ASSERT_TRUE(exact.x == 0 && exact.y == 0 && exact.width == 640 && exact.height == 360);
  const DisplayViewport small = compute_display_viewport(320, 200, design(640, 360));
  ARCANE_ASSERT_TRUE(small.width == 320 && small.height == 180 && small.y == 10);
  ARCANE_ASSERT_TRUE(near(small.scale_x, 0.5f));
}

void test_zero_framebuffer() {
  const DisplayViewport vp = compute_display_viewport(0, 0, design(640, 360));
  ARCANE_ASSERT_TRUE(vp.width == 0 && vp.height == 0);
  ARCANE_ASSERT_TRUE(vp.logical_width == 640 && vp.logical_height == 360);
  const DisplayViewport none = compute_display_viewport(0, 480, {});
  ARCANE_ASSERT_TRUE(none.width == 0 && none.height == 0 && none.logical_width == 0);
}

void test_stretch() {
  const DisplayViewport vp = compute_display_viewport(1280, 360, design(640, 360, DisplayScaleMode::Stretch));
  ARCANE_ASSERT_TRUE(vp.x == 0 && vp.y == 0 && vp.width == 1280 && vp.height == 360);
  ARCANE_ASSERT_TRUE(vp.logical_width == 640 && vp.logical_height == 360);
  ARCANE_ASSERT_TRUE(near(vp.scale_x, 2.0f) && near(vp.scale_y, 1.0f));
}

void test_window_to_logical() {
  const DisplayViewport vp = compute_display_viewport(2000, 900, design(640, 360));  // bars 200 px left/right
  // Window == framebuffer: window centre is the logical centre; the viewport's left edge is logical x 0.
  Vec2 p = window_to_logical({1000.0f, 450.0f}, vp, 900);
  ARCANE_ASSERT_TRUE(near(p.x, 320.0f) && near(p.y, 180.0f));
  p = window_to_logical({200.0f, 0.0f}, vp, 900);
  ARCANE_ASSERT_TRUE(near(p.x, 0.0f) && near(p.y, 0.0f));
  // A point in the left bar is outside the logical view.
  p = window_to_logical({50.0f, 450.0f}, vp, 900);
  ARCANE_ASSERT_TRUE(p.x < 0.0f);
  p = window_to_logical({1950.0f, 450.0f}, vp, 900);
  ARCANE_ASSERT_TRUE(p.x > 640.0f);

  // Tall window: vertical bars; y is measured from the top of the window.
  const DisplayViewport tall = compute_display_viewport(640, 600, design(640, 360));  // y bars 120 px
  p = window_to_logical({100.0f, 120.0f}, tall, 600);
  ARCANE_ASSERT_TRUE(near(p.x, 100.0f) && near(p.y, 0.0f));
  p = window_to_logical({100.0f, 60.0f}, tall, 600);
  ARCANE_ASSERT_TRUE(p.y < 0.0f);
}

void test_window_to_logical_hidpi() {
  // 1000x600 window units, framebuffer 2000x1200 (ratio 2). 640x360 design -> 1.875x, so 1200x675 centred vertically... width-limited:
  const DisplayViewport vp = compute_display_viewport(2000, 1200, design(640, 360));
  ARCANE_ASSERT_TRUE(vp.width == 2000 && vp.height == 1125 && vp.y == 37);
  // Window centre (500, 300) is the logical centre; the framebuffer ratio must be applied.
  Vec2 p = window_to_logical({500.0f, 300.0f}, vp, 1200, {2.0f, 2.0f});
  ARCANE_ASSERT_TRUE(std::fabs(p.x - 320.0f) < 0.05f && std::fabs(p.y - 180.0f) < 0.6f);
  // Without the ratio the same point would be wrong.
  p = window_to_logical({500.0f, 300.0f}, vp, 1200);
  ARCANE_ASSERT_TRUE(std::fabs(p.x - 320.0f) > 100.0f);
  // Stretch: window corner maps to the logical corner.
  const DisplayViewport st = compute_display_viewport(2000, 1200, design(640, 360, DisplayScaleMode::Stretch));
  p = window_to_logical({1000.0f, 600.0f}, st, 1200, {2.0f, 2.0f});
  ARCANE_ASSERT_TRUE(near(p.x, 640.0f) && near(p.y, 360.0f));
}

}  // namespace

int main() {
  test_no_design_passthrough();
  test_letterbox_wide_window();
  test_letterbox_tall_window();
  test_letterbox_exact_and_small();
  test_zero_framebuffer();
  test_stretch();
  test_window_to_logical();
  test_window_to_logical_hidpi();
  return 0;
}
