#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/node.hpp"

namespace arcane {

class ComponentRegistry;
class Scene;

// Highest layer index is 30 so a mask always fits a positive int.
inline constexpr int kMaxCollisionLayers = 31;
inline constexpr int kAllCollisionLayers = 0x7FFFFFFF;

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
  // Runtime-only (not serialized): set by PhysicsWorld::step to true when the body rests on something below it
  // (a touching contact whose normal points mostly up). Used by PlatformerController2D.
  bool grounded = false;
  // Runtime-only: remaining time to ignore one-way platforms.
  float drop_through_time = 0.0f;
  // Runtime-only: impulse queued by apply_impulse, consumed by the next PhysicsWorld::step.
  Vec2 pending_impulse{};
  // Runtime-only: position queued by teleport, applied to the Box2D body at the start of the next step.
  Vec2 pending_teleport{};
  bool has_pending_teleport = false;

  // Overwrites the velocity (pixels/second); takes effect on the next step.
  void set_velocity(Vec2 velocity) noexcept { linear_velocity = velocity; }
  // Moves the body (and its node, at once) to `position`, a scene-space point for a root node, and stops it.
  // Safe to call from any hook: the Box2D body follows at the start of the next PhysicsWorld::step.
  void teleport(Vec2 position) noexcept;
  // Queues an instantaneous impulse in pixel-mass units (px * kg / s, kg = density * shape area in m^2). It is
  // applied after the velocity push of the next step, so the velocity change is impulse / mass; impulses
  // accumulate until then. Static bodies and bodies without a Collider2D (zero mass) are unaffected.
  void apply_impulse(Vec2 impulse) noexcept {
    pending_impulse.x += impulse.x;
    pending_impulse.y += impulse.y;
  }
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
  // A sensor reports overlaps (Component::on_trigger_enter/exit) but never pushes anything.
  bool is_sensor = false;
  // Collision layer index 0..30 (names live in project.json "collision_layers") and the mask of layers this
  // collider reacts to (bit i = layer i). Two colliders collide, or trigger, only if each one's mask has the
  // other's layer. The defaults put everything on layer 0 colliding with everything.
  int layer = 0;
  int mask = kAllCollisionLayers;
  // One-way surfaces support bodies from above; intended for static, unrotated boxes.
  bool one_way = false;
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
  // the Component collision and trigger hooks (see component.hpp) for the
  // shapes that started or stopped touching or overlapping this step.
  void step(Scene& scene, float fixed_dt);

  // Spatial queries in scene pixels against the shapes built by rebuild/step. `layers` is a bit mask of collision
  // layers (bit i = layer i) a hit must be on; sensors are never hit. Results name nodes by id (Scene::find_node);
  // lists are sorted by id so they are deterministic.
  struct RayHit {
    NodeId node = 0;
    Vec2 point{};
    Vec2 normal{};
    float fraction = 0.0f;  // 0..1 along origin -> end
    float distance = 0.0f;  // pixels from origin
  };
  // Closest solid shape on the segment origin -> end.
  std::optional<RayHit> raycast(Vec2 origin, Vec2 end, int layers = kAllCollisionLayers) const;
  // Nodes with a shape overlapping the rectangle [min, max], or containing `point`.
  std::vector<NodeId> overlap_aabb(Vec2 min, Vec2 max, int layers = kAllCollisionLayers) const;
  std::vector<NodeId> overlap_point(Vec2 point, int layers = kAllCollisionLayers) const;

  // Number of Box2D bodies currently in the world.
  int body_count() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace arcane
