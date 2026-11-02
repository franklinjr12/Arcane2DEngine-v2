#include "arcane/display.hpp"

#include <algorithm>
#include <cmath>

namespace arcane {

DisplayViewport compute_display_viewport(int fb_w, int fb_h, const DisplayConfig& display) {
  const bool has_design = display.design_width > 0 && display.design_height > 0;
  DisplayViewport vp;
  vp.logical_width = has_design ? display.design_width : std::max(fb_w, 0);
  vp.logical_height = has_design ? display.design_height : std::max(fb_h, 0);
  if (fb_w <= 0 || fb_h <= 0) return vp;  // minimised: nothing to draw into

  if (!has_design || display.scale_mode == DisplayScaleMode::Stretch) {
    vp.width = fb_w;
    vp.height = fb_h;
  } else {
    const double scale = std::min(static_cast<double>(fb_w) / display.design_width,
                                  static_cast<double>(fb_h) / display.design_height);
    vp.width = std::clamp(static_cast<int>(std::lround(display.design_width * scale)), 1, fb_w);
    vp.height = std::clamp(static_cast<int>(std::lround(display.design_height * scale)), 1, fb_h);
    vp.x = (fb_w - vp.width) / 2;
    vp.y = (fb_h - vp.height) / 2;
  }
  vp.scale_x = static_cast<float>(vp.width) / static_cast<float>(vp.logical_width);
  vp.scale_y = static_cast<float>(vp.height) / static_cast<float>(vp.logical_height);
  return vp;
}

PointerMapping pointer_mapping(const DisplayViewport& vp, int framebuffer_height, Vec2 fb_per_window) {
  const float rx = fb_per_window.x > 0.0f ? fb_per_window.x : 1.0f;
  const float ry = fb_per_window.y > 0.0f ? fb_per_window.y : 1.0f;
  const float sx = vp.scale_x > 0.0f ? vp.scale_x : 1.0f;
  const float sy = vp.scale_y > 0.0f ? vp.scale_y : 1.0f;
  // The viewport's y is measured from the bottom; window y grows downwards.
  const float top = static_cast<float>(framebuffer_height - (vp.y + vp.height));
  PointerMapping m;
  m.origin = {static_cast<float>(vp.x) / rx, top / ry};
  m.scale = {rx / sx, ry / sy};
  return m;
}

Vec2 window_to_logical(Vec2 window_px, const DisplayViewport& vp, int framebuffer_height, Vec2 fb_per_window) {
  const PointerMapping m = pointer_mapping(vp, framebuffer_height, fb_per_window);
  return {(window_px.x - m.origin.x) * m.scale.x, (window_px.y - m.origin.y) * m.scale.y};
}

}  // namespace arcane
