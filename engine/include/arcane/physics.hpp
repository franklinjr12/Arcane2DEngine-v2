#pragma once

#include <memory>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"

namespace arcane {

class ComponentRegistry;
class Scene;

enum class BodyType { Static = 0, Kinematic = 1, Dynamic = 2 };
enum class ColliderShapeType { Box = 0, Circle = 1 };

// A Box2D-backed rigid body. `body_type`/`shape_type` are stored as plain
// `int` (not an enum class) on the component itself so they round-trip
// through PropertyType::Int like every other property -- there is no
// PropertyType::Enum, so int-coded enums are the established pattern (see
// Camera::primary for the bool equivalent). Use the BodyType/ColliderShapeType
// enums above when writing C++ against these fields.
//
// Units: this component's fields are in pixels/seconds, matching every other
// Transform2D-adjacent value in the engine. PhysicsWorld converts to/from
// Box2D's meters at the simulation boundary (32 px = 1 m, see
// docs/decisions.md row 3) -- Box2D's unit expectations never leak out here.
class ARCANE_API RigidBody2D : public Component {
 public:
  int body_type = static_cast<int>(BodyType::Dynamic);
  Vec2 linear_velocity{};
  float gravity_scale = 1.0f;
  bool fixed_rotation = false;
};

// A collision shape attached to the same node as a RigidBody2D. A node with
// a Collider2D but no RigidBody2D is ignored by PhysicsWorld (v1 keeps every
// physical node a body; a "trigger volume with no body" use case can reuse
// a Static RigidBody2D instead of inventing a second code path).
class ARCANE_API Collider2D : public Component {
 public:
  int shape_type = static_cast<int>(ColliderShapeType::Box);
  float width = 32.0f;
  float height = 32.0f;
  float radius = 16.0f;
  Vec2 offset{};
  float density = 1.0f;
  float friction = 0.6f;
  float restitution = 0.0f;
  bool is_sensor = false;
};

ARCANE_API void register_physics_components(ComponentRegistry& registry);

// Owns one Box2D world for one runtime Scene. Never touches the authored
// scene: the editor only ever constructs one of these for the Play-mode
// runtime snapshot (see PlayStopController), and arcane_player constructs
// exactly one for the project scene it runs (which *is* always "Play" for
// the standalone player, there is no Edit mode there).
class ARCANE_API PhysicsWorld {
 public:
  static constexpr float kPixelsPerMeter = 32.0f;

  PhysicsWorld();
  ~PhysicsWorld();
  PhysicsWorld(PhysicsWorld&&) noexcept;
  PhysicsWorld& operator=(PhysicsWorld&&) noexcept;
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  // Creates a Box2D body+shape for every node that has a RigidBody2D
  // (optionally with a sibling Collider2D), replacing any previous bodies.
  // Call once after the runtime scene is built, before the first step().
  void rebuild(Scene& scene);

  // Pushes RigidBody2D::linear_velocity to each body, steps the simulation
  // by `fixed_dt`, pulls resulting position/rotation/velocity back into the
  // owning node's Transform2D and RigidBody2D, and dispatches
  // Component::on_contact_begin/on_contact_end for shapes that started or
  // stopped touching this step.
  void step(Scene& scene, float fixed_dt);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace arcane
