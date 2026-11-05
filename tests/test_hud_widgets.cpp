#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/renderer.hpp"
#include "arcane/screenshot.hpp"
#include "arcane/window.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void test_nine_slice_scales() {
  const HudRect dest{10.0f, 20.0f, 200.0f, 100.0f};
  for (const float s : {1.0f, 2.0f, 3.0f}) {
    const auto p = hud_nine_slice(dest, {8, 8, 8, 8}, 24, 24, s);
    for (const int i : {0, 2, 6, 8}) ARCANE_ASSERT_TRUE(near(p[i].dest.w, 8 * s) && near(p[i].dest.h, 8 * s));
    ARCANE_ASSERT_TRUE(near(p[1].dest.w, 200 - 16 * s) && near(p[1].dest.h, 8 * s));
    ARCANE_ASSERT_TRUE(near(p[3].dest.h, 100 - 16 * s) && near(p[3].dest.w, 8 * s));
    ARCANE_ASSERT_TRUE(near(p[4].dest.w, 200 - 16 * s) && near(p[4].dest.h, 100 - 16 * s));
    float area = 0.0f;
    for (const auto& patch : p) area += patch.dest.w * patch.dest.h;
    ARCANE_ASSERT_TRUE(near(area, 200.0f * 100.0f));
    ARCANE_ASSERT_TRUE(near(p[0].dest.x, 10) && near(p[0].dest.y, 20));
    ARCANE_ASSERT_TRUE(near(p[8].dest.x + p[8].dest.w, 210) && near(p[8].dest.y + p[8].dest.h, 120));
    ARCANE_ASSERT_TRUE(near(p[0].uv.u0, 0) && near(p[0].uv.u1, 1.0f / 3) && near(p[0].uv.v0, 0) && near(p[0].uv.v1, 1.0f / 3));
    ARCANE_ASSERT_TRUE(near(p[8].uv.u0, 2.0f / 3) && near(p[8].uv.u1, 1) && near(p[8].uv.v0, 2.0f / 3) && near(p[8].uv.v1, 1));
  }
  // Dest smaller than the borders: sides shrink to fit exactly, center collapses.
  const auto p = hud_nine_slice({0, 0, 8, 100}, {8, 8, 8, 8}, 24, 24, 1.0f);
  ARCANE_ASSERT_TRUE(near(p[0].dest.w, 4) && near(p[2].dest.w, 4) && near(p[1].dest.w, 0));
  ARCANE_ASSERT_TRUE(near(p[2].dest.x + p[2].dest.w, 8));
}

void test_panel_rect() {
  HudPanel panel;
  panel.anchor = HudAnchor::BottomRight;
  const HudRect r = hud_panel_rect(panel, 2560, 1440);
  ARCANE_ASSERT_TRUE(near(r.w, 400) && near(r.h, 240) && near(r.x + r.w, 2560) && near(r.y + r.h, 1440));
}

void test_box_and_grid() {
  const HudRect area{0, 0, 100, 100};
  const float sizes[] = {10, 20, 30};
  const auto v = hud_layout_box(area, HudAxis::Vertical, sizes, 5, 4);
  ARCANE_ASSERT_TRUE(v.size() == 3);
  ARCANE_ASSERT_TRUE(near(v[0].x, 4) && near(v[0].y, 4) && near(v[0].w, 92) && near(v[0].h, 10));
  ARCANE_ASSERT_TRUE(near(v[1].y, 19) && near(v[2].y, 44) && near(v[2].h, 30));
  const auto h = hud_layout_box(area, HudAxis::Horizontal, sizes, 5, 4);
  ARCANE_ASSERT_TRUE(near(h[1].x, 19) && near(h[2].x, 44) && near(h[1].w, 20) && near(h[1].h, 92) && near(h[1].y, 4));
  const auto g = hud_layout_grid(area, 3, 5, 10, 10, 2, 1);
  ARCANE_ASSERT_TRUE(g.size() == 5);
  ARCANE_ASSERT_TRUE(near(g[0].x, 1) && near(g[0].y, 1) && near(g[2].x, 25) && near(g[3].x, 1) && near(g[3].y, 13));
  ARCANE_ASSERT_TRUE(hud_layout_grid(area, 0, 5, 10, 10, 0, 0).empty());
}

void test_scroll_list() {
  FakeInputBackend backend;
  HudScrollList list;
  list.item_count = 20;
  list.visible_count = 5;
  for (int i = 0; i < 19; ++i) {
    backend.set_down(InputDevice::Keyboard, 264, true);
    list.update(backend);
    ARCANE_ASSERT_TRUE(list.selected == i + 1);
    ARCANE_ASSERT_TRUE(list.first_visible <= list.selected && list.selected < list.first_visible + 5);
    backend.set_down(InputDevice::Keyboard, 264, false);
    list.update(backend);
  }
  ARCANE_ASSERT_TRUE(list.selected == 19 && list.first_visible == 15);
  // Holding the key is one step only; clamped at the end.
  backend.set_down(InputDevice::Keyboard, 264, true);
  list.update(backend);
  list.update(backend);
  ARCANE_ASSERT_TRUE(list.selected == 19);
  backend.set_down(InputDevice::Keyboard, 264, false);
  list.update(backend);
  for (int i = 0; i < 19; ++i) {
    backend.set_gamepad_button(0, GamepadButtons::kDpadUp, true);
    list.update(backend);
    ARCANE_ASSERT_TRUE(list.first_visible <= list.selected && list.selected < list.first_visible + 5);
    backend.set_gamepad_button(0, GamepadButtons::kDpadUp, false);
    list.update(backend);
  }
  ARCANE_ASSERT_TRUE(list.selected == 0 && list.first_visible == 0);
  // Wheel: down (negative) scrolls the window and drags selection along.
  list.update(backend, -3.0f);
  ARCANE_ASSERT_TRUE(list.first_visible == 3 && list.selected == 3);
  list.update(backend, -100.0f);
  ARCANE_ASSERT_TRUE(list.first_visible == 15 && list.selected >= 15 && list.selected < 20);
  list.update(backend, 2.0f);
  ARCANE_ASSERT_TRUE(list.first_visible == 13 && list.selected >= 13 && list.selected < 18);
  list.update(backend, 100.0f);
  ARCANE_ASSERT_TRUE(list.first_visible == 0 && list.selected == 4);
  const auto rects = list.item_rects({0, 0, 100, 100}, 10, 2);
  ARCANE_ASSERT_TRUE(rects.size() == 5 && near(rects[4].y, 48));
  // Confirm edge.
  backend.set_down(InputDevice::Keyboard, Keys::kEnter, true);
  list.update(backend);
  ARCANE_ASSERT_TRUE(list.activated());
  list.update(backend);
  ARCANE_ASSERT_TRUE(!list.activated());
}

void press(HudFocusGroup& g, FakeInputBackend& b, int dpad) {
  b.set_gamepad_button(0, dpad, true);
  g.update(b);
  b.set_gamepad_button(0, dpad, false);
  g.update(b);
}

void test_focus_grid() {
  HudFocusGroup group;
  const auto cells = hud_layout_grid({0, 0, 300, 300}, 3, 9, 50, 50, 10, 0);
  for (std::size_t i = 0; i < cells.size(); ++i) group.items.push_back({"c" + std::to_string(i), cells[i], true});
  FakeInputBackend backend;
  // Any nav press focuses the first enabled item.
  press(group, backend, GamepadButtons::kDpadLeft);
  ARCANE_ASSERT_TRUE(group.focused == 0 && group.focused_id() && *group.focused_id() == "c0");
  press(group, backend, GamepadButtons::kDpadUp);  // edge: stays put
  press(group, backend, GamepadButtons::kDpadLeft);
  ARCANE_ASSERT_TRUE(group.focused == 0);
  const int path[] = {GamepadButtons::kDpadRight, GamepadButtons::kDpadRight, GamepadButtons::kDpadDown,
                      GamepadButtons::kDpadLeft,  GamepadButtons::kDpadLeft,  GamepadButtons::kDpadDown,
                      GamepadButtons::kDpadRight, GamepadButtons::kDpadRight};
  const int expect[] = {1, 2, 5, 4, 3, 6, 7, 8};
  bool visited[9] = {true};
  for (int i = 0; i < 8; ++i) {
    press(group, backend, path[i]);
    ARCANE_ASSERT_TRUE(group.focused == expect[i]);
    visited[group.focused] = true;
  }
  for (const bool v : visited) ARCANE_ASSERT_TRUE(v);
  press(group, backend, GamepadButtons::kDpadRight);  // edge: stays put
  press(group, backend, GamepadButtons::kDpadDown);
  ARCANE_ASSERT_TRUE(group.focused == 8);
  // Disabled items are skipped.
  group.items[7].enabled = false;
  press(group, backend, GamepadButtons::kDpadLeft);
  ARCANE_ASSERT_TRUE(group.focused == 6);
  // Confirm.
  backend.set_gamepad_button(0, GamepadButtons::kA, true);
  group.update(backend);
  ARCANE_ASSERT_TRUE(group.activated());
}

void test_focus_consumes_input() {
  HudFocusGroup group;
  group.items.push_back({"a", {0, 0, 10, 10}, true});
  InputActionMap actions;
  actions.set_action(InputAction{"duck", {{InputDevice::GamepadButton, GamepadButtons::kDpadDown}}});
  FakeInputBackend backend;
  group.focused = 0;
  backend.set_gamepad_button(0, GamepadButtons::kDpadDown, true);
  group.update(backend);
  actions.update(group.gameplay_backend());
  ARCANE_ASSERT_TRUE(!actions.held("duck") && !actions.pressed("duck"));
  group.focused = -1;
  group.items.clear();  // nothing to focus on a press
  group.update(backend);
  actions.update(group.gameplay_backend());
  ARCANE_ASSERT_TRUE(actions.held("duck"));
}

// Golden screenshot of a mock panel with a scroll list; exits 77 without a GL display.
int run_gl_case() {
  constexpr int kW = 160, kH = 120;
  auto window = Window::create({.width = kW, .height = kH, .title = "test_hud_widgets", .hidden = true});
  if (!window || !load_gl_functions()) {
    std::fprintf(stderr, "no GL-capable display available, skipping\n");
    return 77;
  }
  auto renderer = SpriteRenderer::create();
  auto hud = HudRenderer::create();
  ARCANE_ASSERT_TRUE(renderer.has_value() && hud.has_value());

  const unsigned char corner[4] = {20, 30, 110, 255}, edge[4] = {140, 180, 240, 255}, mid[4] = {110, 110, 110, 255};
  std::vector<unsigned char> pixels(24 * 24 * 4);
  for (int y = 0; y < 24; ++y) {
    for (int x = 0; x < 24; ++x) {
      const bool cx = x < 8 || x >= 16, cy = y < 8 || y >= 16;
      const unsigned char* c = (cx && cy) ? corner : (cx || cy) ? edge : mid;
      std::memcpy(&pixels[(static_cast<std::size_t>(y) * 24 + x) * 4], c, 4);
    }
  }
  auto texture = Texture::load_from_pixels(pixels, 24, 24, TextureFilter::Nearest);
  ARCANE_ASSERT_TRUE(texture.has_value());

  int width = 0, height = 0;
  window->framebuffer_size(width, height);
  HudPanel panel;
  panel.texture = &*texture;
  panel.border = {8, 8, 8, 8};
  // Layout units are 720p pixels; the 120px window scales them by 1/6, so these are an 8px inset,
  // a 140x100 px panel and 8px corners (border_scale 6 undoes the scale for the border only).
  const float units = 1.0f / hud_scale(kH);
  panel.border_scale = units;
  panel.offset_x = 8.0f * units;
  panel.offset_y = 8.0f * units;
  panel.width = 140.0f * units;
  panel.height = 100.0f * units;
  panel.tint = {1, 1, 1, 1};
  const HudRect pr = hud_panel_rect(panel, width, height);

  HudScrollList list;
  list.item_count = 20;
  list.visible_count = 4;
  list.selected = 6;
  list.ensure_visible();
  const float pad = 8.0f;
  const HudRect area{pr.x + pad, pr.y + pad, pr.w - 2 * pad, pr.h - 2 * pad};
  const auto rows = list.item_rects(area, 16.0f, 2.0f);

  renderer->begin_frame(width, height, Color{0.05f, 0.05f, 0.1f, 1.0f});
  hud->draw_panel(panel, *renderer, width, height);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Color row = i % 2 == 0 ? Color{0.8f, 0.5f, 0.2f, 1.0f} : Color{0.3f, 0.6f, 0.3f, 1.0f};
    hud->draw_rect(rows[i], *renderer, width, height, row);
    if (list.first_visible + static_cast<int>(i) == list.selected) hud->draw_focus_outline(rows[i], *renderer, height);
  }
  renderer->end_frame();

  auto image = capture_framebuffer(width, height);
  ARCANE_ASSERT_TRUE(image.has_value());
  // Not blank: panel corner and a panel-edge-free center pixel have the texture colors.
  auto px = [&](int x, int y) { return &image->pixels[(static_cast<std::size_t>(y) * width + x) * 4]; };
  const int cxp = static_cast<int>(pr.x) + 2, cyp = static_cast<int>(pr.y) + 2;
  ARCANE_ASSERT_TRUE(std::memcmp(px(cxp, cyp), corner, 3) == 0);
  ARCANE_ASSERT_TRUE(std::memcmp(px(static_cast<int>(pr.x + pr.w * 0.5f), static_cast<int>(pr.y) + 2), edge, 3) == 0);
  // Panel center below the last row (4 rows of 16 + 3 gaps of 2 inside an 84px-high area).
  ARCANE_ASSERT_TRUE(std::memcmp(px(static_cast<int>(pr.x + pr.w * 0.5f), static_cast<int>(pr.y + pr.h) - 10), mid, 3) == 0);

  const std::filesystem::path golden = std::filesystem::path(ARCANE_GOLDEN_DIR) / "render" / "hud_panel.png";
  if (std::getenv("ARCANE_UPDATE_GOLDEN")) {
    ARCANE_ASSERT_TRUE(write_png(golden, *image).has_value());
    std::puts("golden updated");
    return 0;
  }
  auto expected = read_png(golden);
  if (!expected) {
    std::fprintf(stderr, "missing golden %s (run with ARCANE_UPDATE_GOLDEN=1)\n", golden.string().c_str());
    return 1;
  }
  const ImageDiff diff = compare_images(*image, *expected, 2);
  std::printf("size_match=%d max_delta=%d differing=%ld\n", diff.same_size, diff.max_channel_delta, diff.differing_pixels);
  ARCANE_ASSERT_TRUE(diff.same_size);
  ARCANE_ASSERT_TRUE(static_cast<double>(diff.differing_pixels) <= 0.005 * width * height);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--gl") == 0) return run_gl_case();
  test_nine_slice_scales();
  test_panel_rect();
  test_box_and_grid();
  test_scroll_list();
  test_focus_grid();
  test_focus_consumes_input();
  return 0;
}
