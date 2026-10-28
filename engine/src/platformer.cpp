#include "arcane/platformer.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"

namespace arcane {

namespace {

float move_toward(float value, float target, float max_delta) {
  if (std::fabs(target - value) <= max_delta) return target;
  return value + (target > value ? max_delta : -max_delta);
}

}  // namespace

PlatformerController2D::PlatformerController2D() = default;
PlatformerController2D::~PlatformerController2D() = default;

void PlatformerController2D::on_start() {
  if (auto* body = dynamic_cast<RigidBody2D*>(node()->find_component("RigidBody2D"))) {
    base_gravity_scale_ = body->gravity_scale;
  }
}

void PlatformerController2D::on_update(float fixed_dt) {
  auto* body = dynamic_cast<RigidBody2D*>(node()->find_component("RigidBody2D"));
  InputActionMap* input = active_input_map();
  if (!body || !input) return;

  // Timers: coyote refreshes while standing (and not rising from our own jump), the buffer on each press.
  if (body->grounded && body->linear_velocity.y > -1.0f) {
    coyote_timer_ = coyote_time;
    jumping_ = false;
  } else {
    coyote_timer_ = std::max(0.0f, coyote_timer_ - fixed_dt);
  }
  buffer_timer_ = input->pressed(jump_action) ? jump_buffer_time : std::max(0.0f, buffer_timer_ - fixed_dt);

  // Horizontal: accelerate toward the target speed, decelerate to rest with no input.
  float direction = 0.0f;
  if (input->held(left_action)) direction -= 1.0f;
  if (input->held(right_action)) direction += 1.0f;
  const float control = body->grounded ? 1.0f : air_control;
  const float rate = (direction != 0.0f ? run_acceleration : run_deceleration) * control;
  body->linear_velocity.x = move_toward(body->linear_velocity.x, direction * max_speed, rate * fixed_dt);

  // Jump (buffered press + coyote window) and variable height.
  if (buffer_timer_ > 0.0f && coyote_timer_ > 0.0f) {
    body->linear_velocity.y = -jump_speed;
    buffer_timer_ = 0.0f;
    coyote_timer_ = 0.0f;
    jumping_ = true;
  } else if (jumping_ && body->linear_velocity.y < 0.0f && !input->held(jump_action)) {
    body->linear_velocity.y *= jump_cut;
    jumping_ = false;
  }

  body->gravity_scale = body->linear_velocity.y > 0.0f ? base_gravity_scale_ * fall_gravity_scale : base_gravity_scale_;
  body->linear_velocity.y = std::min(body->linear_velocity.y, max_fall_speed);
}

void register_platformer_components(ComponentRegistry& registry) {
  using C = PlatformerController2D;
  registry.register_component(
      "PlatformerController2D", [] { return std::make_unique<C>(); },
      {
          make_property<C>("max_speed", PropertyType::Float, &C::max_speed),
          make_property<C>("run_acceleration", PropertyType::Float, &C::run_acceleration),
          make_property<C>("run_deceleration", PropertyType::Float, &C::run_deceleration),
          make_property<C>("air_control", PropertyType::Float, &C::air_control),
          make_property<C>("jump_speed", PropertyType::Float, &C::jump_speed),
          make_property<C>("fall_gravity_scale", PropertyType::Float, &C::fall_gravity_scale),
          make_property<C>("jump_cut", PropertyType::Float, &C::jump_cut),
          make_property<C>("max_fall_speed", PropertyType::Float, &C::max_fall_speed),
          make_property<C>("coyote_time", PropertyType::Float, &C::coyote_time),
          make_property<C>("jump_buffer_time", PropertyType::Float, &C::jump_buffer_time),
          make_property<C>("left_action", PropertyType::String, &C::left_action),
          make_property<C>("right_action", PropertyType::String, &C::right_action),
          make_property<C>("jump_action", PropertyType::String, &C::jump_action),
      });
}

}  // namespace arcane
