#include "arcane/builtin_components.hpp"

#include <memory>
#include <algorithm>
#include <cmath>

#include "arcane/scene.hpp"
#include "arcane/node.hpp"

#include "arcane/component_registry.hpp"

namespace arcane {

void Camera2D::start_shake() {
  shake_total_ = std::max(0.0f, shake_duration);
  shake_remaining_ = shake_total_;
  random_state_ = static_cast<std::uint32_t>(shake_seed);
  shake_offset_ = {};
}

void Camera2D::on_start() {
  center_ = node()->world_transform().position;
  initialized_ = true;
  start_shake();
}

void Camera2D::advance(const Scene& scene, float dt) {
  if (!initialized_) { center_ = node()->world_transform().position; initialized_ = true; }
  if (const Node* subject = scene.find_node(target)) {
    const Vec2 p = subject->world_transform().position;
    const float dx = p.x - center_.x, dy = p.y - center_.y;
    const float zx = std::max(0.0f, deadzone.x), zy = std::max(0.0f, deadzone.y);
    const float factor = smoothing > 0.0f ? 1.0f - std::exp(-smoothing * std::max(0.0f, dt)) : 1.0f;
    center_.x += (dx - std::clamp(dx, -zx, zx)) * factor;
    center_.y += (dy - std::clamp(dy, -zy, zy)) * factor;
  } else {
    center_ = node()->world_transform().position;
  }
  shake_offset_ = {};
  if (shake_remaining_ > 0.0f && shake_total_ > 0.0f) {
    auto random = [this]() {
      random_state_ = random_state_ * 1664525u + 1013904223u;
      return static_cast<float>(random_state_ >> 8) / 8388607.5f - 1.0f;
    };
    const float strength = std::max(0.0f, shake_amplitude) * shake_remaining_ / shake_total_;
    shake_offset_ = {random() * strength, random() * strength};
    shake_remaining_ = std::max(0.0f, shake_remaining_ - std::max(0.0f, dt));
  }
}

Vec2 Camera2D::view_center(int width, int height) const {
  Vec2 result = initialized_ ? center_ : (node() ? node()->world_transform().position : Vec2{});
  result.x += shake_offset_.x; result.y += shake_offset_.y;
  const float z = zoom > 0.0f ? zoom : 1.0f;
  if (bounds_enabled) {
    auto clamp_axis = [](float value, float lo, float hi, float half) {
      if (hi < lo) std::swap(lo, hi);
      return hi - lo <= 2.0f * half ? (lo + hi) * 0.5f : std::clamp(value, lo + half, hi - half);
    };
    result.x = clamp_axis(result.x, bounds_min.x, bounds_max.x, static_cast<float>(std::max(0, width)) / (2.0f * z));
    result.y = clamp_axis(result.y, bounds_min.y, bounds_max.y, static_cast<float>(std::max(0, height)) / (2.0f * z));
  }
  if (pixel_snap) { result.x = std::round(result.x * z) / z; result.y = std::round(result.y * z) / z; }
  return result;
}

void register_builtin_components(ComponentRegistry& registry) {
  registry.register_component(
      "Sprite", [] { return std::make_unique<Sprite>(); },
      {
          make_property<Sprite>("texture", PropertyType::AssetId, &Sprite::texture),
          make_property<Sprite>("width", PropertyType::Float, &Sprite::width),
          make_property<Sprite>("height", PropertyType::Float, &Sprite::height),
          make_property<Sprite>("tint", PropertyType::Color, &Sprite::tint),
      });
  registry.register_component(
      "Camera2D", [] { return std::make_unique<Camera2D>(); },
      {
          make_property<Camera2D>("primary", PropertyType::Bool, &Camera2D::primary),
          make_property<Camera2D>("zoom", PropertyType::Float, &Camera2D::zoom),
          make_property<Camera2D>("target", PropertyType::EntityId, &Camera2D::target),
          make_property<Camera2D>("smoothing", PropertyType::Float, &Camera2D::smoothing),
          make_property<Camera2D>("deadzone", PropertyType::Vec2, &Camera2D::deadzone),
          make_property<Camera2D>("bounds_enabled", PropertyType::Bool, &Camera2D::bounds_enabled),
          make_property<Camera2D>("bounds_min", PropertyType::Vec2, &Camera2D::bounds_min),
          make_property<Camera2D>("bounds_max", PropertyType::Vec2, &Camera2D::bounds_max),
          make_property<Camera2D>("pixel_snap", PropertyType::Bool, &Camera2D::pixel_snap),
          make_property<Camera2D>("shake_amplitude", PropertyType::Float, &Camera2D::shake_amplitude),
          make_property<Camera2D>("shake_duration", PropertyType::Float, &Camera2D::shake_duration),
          make_property<Camera2D>("shake_seed", PropertyType::Int, &Camera2D::shake_seed),
      });
  registry.register_component(
      "Camera", [] { return std::make_unique<Camera>(); },
      {
          make_property<Camera>("zoom", PropertyType::Float, &Camera::zoom),
          make_property<Camera>("primary", PropertyType::Bool, &Camera::primary),
      });
}

}  // namespace arcane
