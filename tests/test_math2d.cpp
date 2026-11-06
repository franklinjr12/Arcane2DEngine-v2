#include "arcane/math2d.hpp"
#include "test_assert.hpp"

#include <cmath>

using namespace arcane;
using namespace arcane::math;

namespace {
bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool near(Vec2 a, Vec2 b, float eps = 1e-5f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }
constexpr float deg(float d) { return d * kPi / 180.0f; }
}  // namespace

int main() {
  // Vec2 arithmetic.
  Vec2 a{1.0f, 2.0f}, b{3.0f, -4.0f};
  ARCANE_ASSERT_TRUE((a + b) == (Vec2{4.0f, -2.0f}));
  ARCANE_ASSERT_TRUE((a - b) == (Vec2{-2.0f, 6.0f}));
  ARCANE_ASSERT_TRUE(-a == (Vec2{-1.0f, -2.0f}));
  ARCANE_ASSERT_TRUE((a * 2.0f) == (Vec2{2.0f, 4.0f}));
  ARCANE_ASSERT_TRUE((2.0f * a) == (Vec2{2.0f, 4.0f}));
  ARCANE_ASSERT_TRUE((b / 2.0f) == (Vec2{1.5f, -2.0f}));
  Vec2 c = a;
  c += b;
  ARCANE_ASSERT_TRUE(c == (Vec2{4.0f, -2.0f}));
  c -= b;
  ARCANE_ASSERT_TRUE(c == a);
  c *= 3.0f;
  ARCANE_ASSERT_TRUE(c == (Vec2{3.0f, 6.0f}));
  c /= 3.0f;
  ARCANE_ASSERT_TRUE(c == a);

  // dot, cross, length, distance.
  ARCANE_ASSERT_TRUE(near(dot(a, b), -5.0f));
  ARCANE_ASSERT_TRUE(near(cross(a, b), -10.0f));
  ARCANE_ASSERT_TRUE(near(cross({1, 0}, {0, 1}), 1.0f));
  ARCANE_ASSERT_TRUE(near(length_sq(b), 25.0f));
  ARCANE_ASSERT_TRUE(near(length(b), 5.0f));
  ARCANE_ASSERT_TRUE(near(distance({1, 1}, {4, 5}), 5.0f));

  // normalize.
  ARCANE_ASSERT_TRUE(near(length(normalize(b)), 1.0f));
  ARCANE_ASSERT_TRUE(near(normalize(b), {0.6f, -0.8f}));
  const Vec2 z{0.0f, 0.0f};
  ARCANE_ASSERT_TRUE(length(z) == 0.0f);
  const Vec2 nz = normalize(z);
  ARCANE_ASSERT_TRUE(nz == z);
  ARCANE_ASSERT_TRUE(!std::isnan(nz.x) && !std::isnan(nz.y));
  const Vec2 tiny = normalize({1e-30f, 0.0f});
  ARCANE_ASSERT_TRUE(!std::isnan(tiny.x) && !std::isnan(tiny.y));

  // lerp.
  ARCANE_ASSERT_TRUE(lerp(2.0f, 6.0f, 0.0f) == 2.0f);
  ARCANE_ASSERT_TRUE(lerp(2.0f, 6.0f, 1.0f) == 6.0f);
  ARCANE_ASSERT_TRUE(near(lerp(2.0f, 6.0f, 0.5f), 4.0f));
  ARCANE_ASSERT_TRUE(lerp(a, b, 0.0f) == a);
  ARCANE_ASSERT_TRUE(lerp(a, b, 1.0f) == b);
  ARCANE_ASSERT_TRUE(near(lerp(a, b, 0.5f), {2.0f, -1.0f}));

  // clamp.
  ARCANE_ASSERT_TRUE(clamp(-5.0f, 0.0f, 10.0f) == 0.0f);
  ARCANE_ASSERT_TRUE(clamp(5.0f, 0.0f, 10.0f) == 5.0f);
  ARCANE_ASSERT_TRUE(clamp(15.0f, 0.0f, 10.0f) == 10.0f);

  // approach.
  ARCANE_ASSERT_TRUE(near(approach(0.0f, 10.0f, 3.0f), 3.0f));
  ARCANE_ASSERT_TRUE(near(approach(10.0f, 0.0f, 3.0f), 7.0f));
  ARCANE_ASSERT_TRUE(approach(9.0f, 10.0f, 3.0f) == 10.0f);
  ARCANE_ASSERT_TRUE(approach(1.0f, 0.0f, 3.0f) == 0.0f);
  ARCANE_ASSERT_TRUE(approach(5.0f, 10.0f, 5.0f) == 10.0f);
  ARCANE_ASSERT_TRUE(approach(5.0f, 5.0f, 2.0f) == 5.0f);
  ARCANE_ASSERT_TRUE(approach(5.0f, 10.0f, 0.0f) == 5.0f);

  // wrap_angle: [-pi, pi).
  ARCANE_ASSERT_TRUE(near(wrap_angle(0.0f), 0.0f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(3.0f * kPi), -kPi, 1e-4f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(-3.0f * kPi), -kPi, 1e-4f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(kPi), -kPi));
  ARCANE_ASSERT_TRUE(near(wrap_angle(-kPi), -kPi));
  ARCANE_ASSERT_TRUE(wrap_angle(kPi + 0.01f) < 0.0f && near(wrap_angle(kPi + 0.01f), -kPi + 0.01f, 1e-4f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(deg(190.0f)), deg(-170.0f), 1e-5f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(deg(-190.0f)), deg(170.0f), 1e-5f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(100.0f * kTwoPi + 0.5f), 0.5f, 1e-3f));
  ARCANE_ASSERT_TRUE(near(wrap_angle(-100.0f * kTwoPi + 0.5f), 0.5f, 1e-3f));
  for (float r = -50.0f; r <= 50.0f; r += 0.37f) {
    const float w = wrap_angle(r);
    ARCANE_ASSERT_TRUE(w >= -kPi && w < kPi);
  }

  // shortest_angle_delta across the seam.
  ARCANE_ASSERT_TRUE(near(shortest_angle_delta(deg(170.0f), deg(-170.0f)), deg(20.0f), 1e-5f));
  ARCANE_ASSERT_TRUE(near(shortest_angle_delta(deg(-170.0f), deg(170.0f)), deg(-20.0f), 1e-5f));
  ARCANE_ASSERT_TRUE(near(shortest_angle_delta(deg(10.0f), deg(50.0f)), deg(40.0f), 1e-5f));
  ARCANE_ASSERT_TRUE(near(shortest_angle_delta(1.0f, 1.0f), 0.0f));

  // Rect contains: half-open.
  const Rect r{10.0f, 20.0f, 30.0f, 40.0f};
  ARCANE_ASSERT_TRUE(contains(r, {25.0f, 40.0f}));
  ARCANE_ASSERT_TRUE(contains(r, {10.0f, 20.0f}));
  ARCANE_ASSERT_TRUE(!contains(r, {40.0f, 40.0f}));
  ARCANE_ASSERT_TRUE(!contains(r, {25.0f, 60.0f}));
  ARCANE_ASSERT_TRUE(!contains(r, {9.9f, 40.0f}));
  ARCANE_ASSERT_TRUE(!contains(Rect{}, {0.0f, 0.0f}));

  // Rect intersects / intersection.
  const Rect o{30.0f, 40.0f, 30.0f, 40.0f};
  ARCANE_ASSERT_TRUE(intersects(r, o) && intersects(o, r));
  ARCANE_ASSERT_TRUE(intersection(r, o) == (Rect{30.0f, 40.0f, 10.0f, 20.0f}));
  const Rect touch{40.0f, 20.0f, 10.0f, 10.0f};
  ARCANE_ASSERT_TRUE(!intersects(r, touch));
  ARCANE_ASSERT_TRUE(intersection(r, touch) == Rect{});
  const Rect far{100.0f, 100.0f, 5.0f, 5.0f};
  ARCANE_ASSERT_TRUE(!intersects(r, far));
  ARCANE_ASSERT_TRUE(intersection(r, far) == Rect{});
  ARCANE_ASSERT_TRUE(!intersects(r, Rect{15.0f, 25.0f, 0.0f, 5.0f}));
  ARCANE_ASSERT_TRUE(!intersects(Rect{}, Rect{}));
  const Rect inner{15.0f, 25.0f, 5.0f, 5.0f};
  ARCANE_ASSERT_TRUE(intersection(r, inner) == inner);

  // union_rect.
  ARCANE_ASSERT_TRUE(union_rect(r, o) == (Rect{10.0f, 20.0f, 50.0f, 60.0f}));
  ARCANE_ASSERT_TRUE(union_rect(r, far) == (Rect{10.0f, 20.0f, 95.0f, 85.0f}));
  ARCANE_ASSERT_TRUE(union_rect(Rect{}, r) == r);
  ARCANE_ASSERT_TRUE(union_rect(r, Rect{}) == r);
  ARCANE_ASSERT_TRUE(union_rect(Rect{}, Rect{}) == Rect{});

  return 0;
}
