#include "arcane/topdown.hpp"

#include <cmath>
#include <memory>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"

namespace arcane {

namespace {

Vec2 move_toward(Vec2 value, Vec2 target, float max_delta) {
  const float dx = target.x - value.x;
  const float dy = target.y - value.y;
  const float distance = std::sqrt(dx * dx + dy * dy);
  if (distance <= max_delta || distance == 0.0f) return target;
  const float scale = max_delta / distance;
  return {value.x + dx * scale, value.y + dy * scale};
}

}  // namespace

TopDownController2D::TopDownController2D() = default;
TopDownController2D::~TopDownController2D() = default;

void TopDownController2D::on_update(float fixed_dt) {
  auto* body = dynamic_cast<RigidBody2D*>(node()->find_component("RigidBody2D"));
  InputActionMap* input = active_input_map();
  if (!body || !input) return;

  Vec2 dir;
  if (input->held(left_action)) dir.x -= 1.0f;
  if (input->held(right_action)) dir.x += 1.0f;
  if (input->held(up_action)) dir.y -= 1.0f;
  if (input->held(down_action)) dir.y += 1.0f;
  moving_ = dir.x != 0.0f || dir.y != 0.0f;
  if (moving_) {
    const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    facing_ = {dir.x / length, dir.y / length};
    if (normalize_diagonal) dir = facing_;
  }

  const Vec2 target{dir.x * max_speed, dir.y * max_speed};
  body->linear_velocity =
      move_toward(body->linear_velocity, target, (moving_ ? acceleration : deceleration) * fixed_dt);

  if (flip_sprite && facing_.x != 0.0f) {
    if (auto* sprite = dynamic_cast<Sprite*>(node()->find_component("Sprite"))) sprite->flip_x = facing_.x < 0.0f;
  }
}

void register_topdown_components(ComponentRegistry& registry) {
  using C = TopDownController2D;
  registry.register_component(
      "TopDownController2D", [] { return std::make_unique<C>(); },
      {
          make_property<C>("max_speed", PropertyType::Float, &C::max_speed),
          make_property<C>("acceleration", PropertyType::Float, &C::acceleration),
          make_property<C>("deceleration", PropertyType::Float, &C::deceleration),
          make_property<C>("normalize_diagonal", PropertyType::Bool, &C::normalize_diagonal),
          make_property<C>("left_action", PropertyType::String, &C::left_action),
          make_property<C>("right_action", PropertyType::String, &C::right_action),
          make_property<C>("up_action", PropertyType::String, &C::up_action),
          make_property<C>("down_action", PropertyType::String, &C::down_action),
          make_property<C>("flip_sprite", PropertyType::Bool, &C::flip_sprite),
      });
}

}  // namespace arcane
