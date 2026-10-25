#pragma once

namespace arcane {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  friend bool operator==(const Vec2&, const Vec2&) = default;
};

}  // namespace arcane
