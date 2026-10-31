#pragma once

#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/project.hpp"

namespace arcane {

// Where the game view lands inside a framebuffer (pixels, origin bottom-left as
// GL's viewport; x/y/width/height is the rectangle the game is drawn in). The
// logical size is what game code, the camera and the HUD lay out in; scale is
// framebuffer pixels per logical pixel.
struct DisplayViewport {
  int x = 0, y = 0, width = 0, height = 0;
  int logical_width = 0, logical_height = 0;
  float scale_x = 1.0f, scale_y = 1.0f;
  friend bool operator==(const DisplayViewport&, const DisplayViewport&) = default;
};

// No design size: the whole framebuffer at scale 1. Letterbox: the largest
// uniform fit, centred (integer rect, bars around it). Stretch: the whole
// framebuffer, scaled non-uniformly. A framebuffer with a zero side (minimised
// window) gives an all-zero rectangle with the design (or 0) logical size.
ARCANE_API DisplayViewport compute_display_viewport(int framebuffer_width, int framebuffer_height,
                                                    const DisplayConfig& display);

// Pointer mapping from window coordinates (origin top-left, window units) to
// logical pixels (origin top-left): logical = (window - origin) * scale.
// `fb_per_window` is framebuffer size / window size (HiDPI: 2 on a Retina display).
struct PointerMapping {
  Vec2 origin{};
  Vec2 scale{1.0f, 1.0f};
};
ARCANE_API PointerMapping pointer_mapping(const DisplayViewport& viewport, int framebuffer_height,
                                          Vec2 fb_per_window = {1.0f, 1.0f});

// Points in the bars map outside [0, logical size).
ARCANE_API Vec2 window_to_logical(Vec2 window_px, const DisplayViewport& viewport, int framebuffer_height,
                                  Vec2 fb_per_window = {1.0f, 1.0f});

}  // namespace arcane
