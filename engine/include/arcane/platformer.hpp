#pragma once

#include <string>

#include "arcane/component.hpp"
#include "arcane/export.hpp"

namespace arcane {

class ComponentRegistry;

// Data-driven platformer character controls for a node that also has a RigidBody2D (+ Collider2D). Runs in
// on_update (fixed step, before PhysicsWorld::step), reads the active InputActionMap, and drives the sibling
// RigidBody2D's linear_velocity / gravity_scale. Ground state comes from RigidBody2D::grounded, which
// PhysicsWorld fills from Box2D contact normals, so it works against any collider, tilemaps included.
// Units: pixels and seconds, y-down (up is negative y). Give the collider low friction so it does not stick to walls.
// No-op without a sibling RigidBody2D or an active input map.
class ARCANE_API PlatformerController2D : public Component {
 public:
  PlatformerController2D();
  ~PlatformerController2D() override;

  float max_speed = 200.0f;          // horizontal cap, px/s
  float run_acceleration = 1400.0f;  // px/s^2 while a direction is held
  float run_deceleration = 1800.0f;  // px/s^2 when no direction is held
  float air_control = 0.6f;          // multiplier on accel/decel while airborne
  float jump_speed = 380.0f;         // initial upward speed, px/s
  float fall_gravity_scale = 1.6f;   // gravity multiplier while falling (relative to the body's own scale)
  float jump_cut = 0.5f;             // vertical velocity multiplier when jump is released while rising
  float max_fall_speed = 700.0f;     // px/s
  float coyote_time = 0.1f;          // seconds after leaving the ground a jump is still allowed
  float jump_buffer_time = 0.1f;     // seconds a jump press is remembered before landing
  std::string left_action = "move_left";
  std::string right_action = "move_right";
  std::string jump_action = "jump";
  std::string down_action = "move_down";
  float drop_duration = 0.5f;

  void on_start() override;
  void on_update(float fixed_dt) override;

 private:
  float base_gravity_scale_ = 1.0f;
  float coyote_timer_ = 0.0f;
  float buffer_timer_ = 0.0f;
  bool jumping_ = false;  // rising from a jump we started; enables the jump cut
};

ARCANE_API void register_platformer_components(ComponentRegistry& registry);

}  // namespace arcane
