#pragma once

#include <string>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"

namespace arcane {

class ComponentRegistry;

// Data-driven top-down character movement for a node that also has a RigidBody2D (set gravity_scale 0).
// Runs in on_update (fixed step, before PhysicsWorld::step), reads the active InputActionMap and moves the
// sibling RigidBody2D's linear_velocity toward direction * max_speed. Units: pixels and seconds, y-down
// (up is negative y). No-op without a sibling RigidBody2D or an active input map.
class ARCANE_API TopDownController2D : public Component {
 public:
  TopDownController2D();
  ~TopDownController2D() override;

  float max_speed = 160.0f;       // px/s
  float acceleration = 1200.0f;   // px/s^2 while a direction is held
  float deceleration = 1600.0f;   // px/s^2 when no direction is held
  bool normalize_diagonal = true;  // diagonal input has the same speed as straight input
  std::string left_action = "left";
  std::string right_action = "right";
  std::string up_action = "up";
  std::string down_action = "down";
  bool flip_sprite = false;  // set the sibling Sprite's flip_x from the horizontal facing

  void on_update(float fixed_dt) override;

  // Runtime state, never serialized.
  Vec2 facing() const { return facing_; }  // last non-zero input direction, normalized
  bool moving() const { return moving_; }  // true while a direction is held

 private:
  Vec2 facing_{1.0f, 0.0f};
  bool moving_ = false;
};

ARCANE_API void register_topdown_components(ComponentRegistry& registry);

}  // namespace arcane
