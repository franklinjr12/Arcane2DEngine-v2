#pragma once

#include <algorithm>
#include <cmath>

namespace arcane {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  friend bool operator==(const Vec2&, const Vec2&) = default;
};

// Vec2 arithmetic (found by ADL).
constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(Vec2 v) { return {-v.x, -v.y}; }
constexpr Vec2 operator*(Vec2 v, float s) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator*(float s, Vec2 v) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator/(Vec2 v, float s) { return {v.x / s, v.y / s}; }
constexpr Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }
constexpr Vec2& operator-=(Vec2& a, Vec2 b) { a = a - b; return a; }
constexpr Vec2& operator*=(Vec2& v, float s) { v = v * s; return v; }
constexpr Vec2& operator/=(Vec2& v, float s) { v = v / s; return v; }

// Axis-aligned rectangle: min corner and size, half-open [x, x+w) x [y, y+h).
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  friend bool operator==(const Rect&, const Rect&) = default;
};

// In a nested namespace: navigation.cpp and particles.cpp define their own
// unqualified length/lerp in arcane::{anonymous}.
namespace math {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

// Vector products and lengths.
constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
constexpr float length_sq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(length_sq(v)); }
inline float distance(Vec2 a, Vec2 b) { return length(b - a); }

// Unit vector; {0,0} for zero or tiny input (never NaN).
inline Vec2 normalize(Vec2 v) {
  const float len = length(v);
  if (!(len > 1e-12f)) return {};
  return v / len;
}

// Scalar helpers.
constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
constexpr float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Moves current toward target by at most max_delta (>= 0), never overshooting.
constexpr float approach(float current, float target, float max_delta) {
  if (current < target) return current + max_delta < target ? current + max_delta : target;
  return current - max_delta > target ? current - max_delta : target;
}

// Angles in radians; wrapped range is [-pi, pi).
inline float wrap_angle(float radians) {
  float r = std::fmod(radians + kPi, kTwoPi);
  if (r < 0.0f) r += kTwoPi;
  if (r >= kTwoPi) r = 0.0f;
  return r - kPi;
}
inline float shortest_angle_delta(float from, float to) { return wrap_angle(to - from); }

// Rect queries; empty rects (w <= 0 or h <= 0) contain and intersect nothing.
constexpr bool contains(const Rect& r, Vec2 p) {
  return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}
constexpr bool intersects(const Rect& a, const Rect& b) {
  if (!(a.w > 0.0f && a.h > 0.0f && b.w > 0.0f && b.h > 0.0f)) return false;
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
// Overlap region, or {} when the rects do not overlap.
constexpr Rect intersection(const Rect& a, const Rect& b) {
  if (!intersects(a, b)) return {};
  const float x0 = a.x > b.x ? a.x : b.x;
  const float y0 = a.y > b.y ? a.y : b.y;
  const float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
  const float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 - x0, y1 - y0};
}
// Bounding box; an empty operand yields the other one.
constexpr Rect union_rect(const Rect& a, const Rect& b) {
  const bool ea = !(a.w > 0.0f && a.h > 0.0f);
  const bool eb = !(b.w > 0.0f && b.h > 0.0f);
  if (ea) return b;
  if (eb) return a;
  const float x0 = a.x < b.x ? a.x : b.x;
  const float y0 = a.y < b.y ? a.y : b.y;
  const float x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  const float y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace math
}  // namespace arcane
