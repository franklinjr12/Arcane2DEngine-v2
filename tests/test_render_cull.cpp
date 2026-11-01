// Pure view-culling math: no window or GL context needed.
#include <cmath>

#include "arcane/renderer.hpp"
#include "test_assert.hpp"

using arcane::SpriteDraw;
using arcane::sprite_in_view;

namespace {
SpriteDraw sprite_at(float x, float y, float w, float h, float rotation = 0.0f) {
  return SpriteDraw{.x = x, .y = y, .width = w, .height = h, .rotation_radians = rotation};
}
}  // namespace

int main() {
  // 200x100 view centered on the origin: x in [-100,100], y in [-50,50].
  const float vw = 200.0f, vh = 100.0f;

  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(0, 0, 10, 10), 0, 0, 1.0f, vw, vh));

  // Fully outside each side (sprites are 10x10, centered).
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(-120, 0, 10, 10), 0, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(120, 0, 10, 10), 0, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(0, -70, 10, 10), 0, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(0, 70, 10, 10), 0, 0, 1.0f, vw, vh));

  // Partly overlapping an edge.
  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(103, 0, 10, 10), 0, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(0, -53, 10, 10), 0, 0, 1.0f, vw, vh));

  // Camera offset moves the visible rect.
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(0, 0, 10, 10), 500, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(500, 0, 10, 10), 500, 0, 1.0f, vw, vh));

  // Zoom 2 halves the visible rect (x in [-50,50]); zoom 0.5 doubles it (x in [-200,200]).
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(80, 0, 10, 10), 0, 0, 2.0f, vw, vh));
  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(80, 0, 10, 10), 0, 0, 1.0f, vw, vh));
  ARCANE_ASSERT_TRUE(!sprite_in_view(sprite_at(230, 0, 10, 10), 0, 0, 0.5f, vw, vh));
  ARCANE_ASSERT_TRUE(sprite_in_view(sprite_at(150, 0, 10, 10), 0, 0, 0.5f, vw, vh));

  // Origin offsets: top-left origin sprite at x=100 extends right of the view edge.
  SpriteDraw corner = sprite_at(100, 0, 10, 10);
  corner.origin_x = 0.0f;
  corner.origin_y = 0.0f;
  ARCANE_ASSERT_TRUE(sprite_in_view(corner, 0, 0, 1.0f, vw, vh));
  corner.x = 101;
  ARCANE_ASSERT_TRUE(!sprite_in_view(corner, 0, 0, 1.0f, vw, vh));

  // Long thin sprite: unrotated AABB (x in [130,230]) is outside, but rotated
  // 90 degrees around its pivot at x=130 it reaches the view, so it must be visible.
  SpriteDraw thin = sprite_at(130, 0, 200, 2);
  thin.origin_x = 0.0f;
  thin.origin_y = 0.5f;
  ARCANE_ASSERT_TRUE(!sprite_in_view(thin, 0, 0, 1.0f, vw, vh));
  thin.x = 100;
  thin.y = -150;
  ARCANE_ASSERT_TRUE(!sprite_in_view(thin, 0, 0, 1.0f, vw, vh));
  thin.rotation_radians = 1.5707963f;  // points +y from y=-150 to y=50
  ARCANE_ASSERT_TRUE(sprite_in_view(thin, 0, 0, 1.0f, vw, vh));

  // A rotated sprite far away is still culled.
  thin.x = 1000;
  ARCANE_ASSERT_TRUE(!sprite_in_view(thin, 0, 0, 1.0f, vw, vh));
  return 0;
}
