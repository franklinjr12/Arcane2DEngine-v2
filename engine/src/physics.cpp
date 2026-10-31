#include "arcane/physics.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/platformer.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"

namespace arcane {

namespace {

constexpr float kPxPerM = PhysicsWorld::kPixelsPerMeter;

float px_to_m(float px) { return px / kPxPerM; }
float m_to_px(float m) { return m * kPxPerM; }
// Engine space is y-down (screen); Box2D is y-up. The flip lives only here: every position, offset, velocity and
// angle crossing the boundary has y (or the rotation sign) negated.
b2Vec2 px_to_m(const Vec2& v) { return b2Vec2{px_to_m(v.x), -px_to_m(v.y)}; }
Vec2 m_to_px(const b2Vec2& v) { return Vec2{m_to_px(v.x), -m_to_px(v.y)}; }

void* node_id_to_user_data(NodeId id) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)); }
NodeId user_data_to_node_id(void* ptr) { return static_cast<NodeId>(reinterpret_cast<std::uintptr_t>(ptr)); }

void collect_physics_nodes(const std::vector<std::unique_ptr<Node>>& nodes, std::vector<Node*>& out) {
  for (const auto& node : nodes) {
    out.push_back(node.get());
    collect_physics_nodes(node->children(), out);
  }
}

void notify_contact(Scene& scene, b2ShapeId shape_a, b2ShapeId shape_b, bool begin) {
  const NodeId id_a = user_data_to_node_id(b2Body_GetUserData(b2Shape_GetBody(shape_a)));
  const NodeId id_b = user_data_to_node_id(b2Body_GetUserData(b2Shape_GetBody(shape_b)));
  Node* node_a = scene.find_node(id_a);
  Node* node_b = scene.find_node(id_b);
  if (!node_a || !node_b) return;
  for (const auto& component : node_a->components()) {
    if (begin) component->on_contact_begin(*node_b); else component->on_contact_end(*node_b);
  }
  for (const auto& component : node_b->components()) {
    if (begin) component->on_contact_begin(*node_a); else component->on_contact_end(*node_a);
  }
}

// Shape user data marks one-way colliders; body user data is the scene node id.
void one_way_pre_solve(b2ShapeId a, b2ShapeId b, b2Manifold* manifold, void* context) {
  auto* scene = static_cast<Scene*>(context);
  const bool a_one_way = b2Shape_GetUserData(a) != nullptr;
  const bool b_one_way = b2Shape_GetUserData(b) != nullptr;
  if (a_one_way == b_one_way) return;
  const b2ShapeId other = a_one_way ? b : a;
  Node* node = scene->find_node(user_data_to_node_id(b2Body_GetUserData(b2Shape_GetBody(other))));
  auto* rigid = node ? dynamic_cast<RigidBody2D*>(node->find_component("RigidBody2D")) : nullptr;
  const float up = a_one_way ? manifold->normal.y : -manifold->normal.y;
  const b2Vec2 relative = b2Sub(b2Body_GetLinearVelocity(b2Shape_GetBody(other)),
                                b2Body_GetLinearVelocity(b2Shape_GetBody(a_one_way ? a : b)));
  if (up < 0.7f || relative.y > 0.01f || (rigid && rigid->drop_through_time > 0.0f))
    manifold->pointCount = 0;
}

// True if any touching contact pushes up on `body` (manifold normal points from shape A to B, in y-up Box2D space).
bool is_grounded(b2BodyId body) {
  constexpr float kMinUpwardNormal = 0.7f;
  b2ContactData contacts[16];
  const int count = b2Body_GetContactData(body, contacts, 16);
  for (int i = 0; i < count; ++i) {
    if (contacts[i].manifold.pointCount == 0) continue;
    const bool body_is_a = B2_ID_EQUALS(b2Shape_GetBody(contacts[i].shapeIdA), body);
    const float up = body_is_a ? -contacts[i].manifold.normal.y : contacts[i].manifold.normal.y;
    if (up > kMinUpwardNormal) return true;
  }
  return false;
}

}  // namespace

void RigidBody2D::teleport(Vec2 position) noexcept {
  pending_teleport = position;
  has_pending_teleport = true;
  linear_velocity = {};
  if (node()) node()->local_transform().position = position;
}

void register_physics_components(ComponentRegistry& registry) {
  registry.register_component(
      "RigidBody2D", [] { return std::make_unique<RigidBody2D>(); },
      {
          make_property<RigidBody2D>("body_type", PropertyType::Int, &RigidBody2D::body_type),
          make_property<RigidBody2D>("linear_velocity", PropertyType::Vec2, &RigidBody2D::linear_velocity),
          make_property<RigidBody2D>("gravity_scale", PropertyType::Float, &RigidBody2D::gravity_scale),
          make_property<RigidBody2D>("fixed_rotation", PropertyType::Bool, &RigidBody2D::fixed_rotation),
      });
  registry.register_component(
      "Collider2D", [] { return std::make_unique<Collider2D>(); },
      {
          make_property<Collider2D>("shape_type", PropertyType::Int, &Collider2D::shape_type),
          make_property<Collider2D>("width", PropertyType::Float, &Collider2D::width),
          make_property<Collider2D>("height", PropertyType::Float, &Collider2D::height),
          make_property<Collider2D>("radius", PropertyType::Float, &Collider2D::radius),
          make_property<Collider2D>("offset", PropertyType::Vec2, &Collider2D::offset),
          make_property<Collider2D>("density", PropertyType::Float, &Collider2D::density),
          make_property<Collider2D>("friction", PropertyType::Float, &Collider2D::friction),
          make_property<Collider2D>("restitution", PropertyType::Float, &Collider2D::restitution),
          make_property<Collider2D>("is_sensor", PropertyType::Bool, &Collider2D::is_sensor),
          make_property<Collider2D>("one_way", PropertyType::Bool, &Collider2D::one_way),
      });
  register_platformer_components(registry);
}

struct PhysicsWorld::Impl {
  b2WorldId world = b2_nullWorldId;
  std::unordered_map<NodeId, b2BodyId> bodies;

  void add_node(Node* node);

  ~Impl() {
    if (b2World_IsValid(world)) b2DestroyWorld(world);
  }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {
  b2WorldDef def = b2DefaultWorldDef();
  def.gravity = {0.0f, -10.0f};
  impl_->world = b2CreateWorld(&def);
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

int PhysicsWorld::body_count() const noexcept { return static_cast<int>(impl_->bodies.size()); }

void PhysicsWorld::Impl::add_node(Node* node) {
  auto* rigid = dynamic_cast<RigidBody2D*>(node->find_component("RigidBody2D"));
  const WorldTransform2D world_transform = node->world_transform();
  if (!rigid) {
    // A tilemap without its own body gets one static body holding a box per merged run of solid tiles.
    auto* tilemap = dynamic_cast<Tilemap*>(node->find_component("Tilemap"));
    if (!tilemap || !tilemap->collide) return;
    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type = b2_staticBody;
    body_def.position = px_to_m(world_transform.position);
    body_def.userData = node_id_to_user_data(node->id());
    b2BodyId body = b2CreateBody(world, &body_def);
    bodies[node->id()] = body;
    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.enableContactEvents = true;
    for (const TileRect& r : tilemap->solid_rects()) {
      const Vec2 half{r.width * world_transform.scale.x * 0.5f, r.height * world_transform.scale.y * 0.5f};
      const Vec2 center{r.x * world_transform.scale.x + half.x, r.y * world_transform.scale.y + half.y};
      b2Polygon box = b2MakeOffsetBox(px_to_m(half.x), px_to_m(half.y), px_to_m(center), b2Rot_identity);
      b2CreatePolygonShape(body, &shape_def, &box);
    }
    return;
  }

  b2BodyDef body_def = b2DefaultBodyDef();
  body_def.type = static_cast<b2BodyType>(rigid->body_type);
  body_def.position = px_to_m(world_transform.position);
  body_def.rotation = b2MakeRot(-world_transform.rotation_radians);
  body_def.linearVelocity = px_to_m(rigid->linear_velocity);
  body_def.gravityScale = rigid->gravity_scale;
  body_def.motionLocks.angularZ = rigid->fixed_rotation;
  body_def.userData = node_id_to_user_data(node->id());

  b2BodyId body = b2CreateBody(world, &body_def);
  bodies[node->id()] = body;

  auto* collider = dynamic_cast<Collider2D*>(node->find_component("Collider2D"));
  if (collider) {
    b2ShapeDef shape_def = b2DefaultShapeDef();
    shape_def.density = collider->density;
    shape_def.material.friction = collider->friction;
    shape_def.material.restitution = collider->restitution;
    shape_def.isSensor = collider->is_sensor;
    shape_def.enableSensorEvents = collider->is_sensor;
    shape_def.enableContactEvents = true;
    shape_def.enablePreSolveEvents = collider->one_way;
    shape_def.userData = collider->one_way ? collider : nullptr;
    const b2Vec2 offset_m = px_to_m(collider->offset);

    if (static_cast<ColliderShapeType>(collider->shape_type) == ColliderShapeType::Circle) {
      b2Circle circle{offset_m, px_to_m(collider->radius)};
      b2CreateCircleShape(body, &shape_def, &circle);
    } else {
      b2Polygon box = b2MakeOffsetBox(px_to_m(collider->width) * 0.5f, px_to_m(collider->height) * 0.5f, offset_m,
                                       b2Rot_identity);
      b2CreatePolygonShape(body, &shape_def, &box);
    }
  }
}

void PhysicsWorld::rebuild(Scene& scene) {
  b2World_SetPreSolveCallback(impl_->world, one_way_pre_solve, nullptr, &scene);
  for (auto& [id, body] : impl_->bodies) {
    if (b2Body_IsValid(body)) b2DestroyBody(body);
  }
  impl_->bodies.clear();

  std::vector<Node*> nodes;
  collect_physics_nodes(scene.roots(), nodes);
  for (Node* node : nodes) impl_->add_node(node);
}

void PhysicsWorld::step(Scene& scene, float fixed_dt) {
  // Nodes destroyed since the last step lose their body; nodes spawned since then gain one.
  for (auto it = impl_->bodies.begin(); it != impl_->bodies.end();) {
    if (scene.find_node(it->first)) {
      ++it;
      continue;
    }
    if (b2Body_IsValid(it->second)) b2DestroyBody(it->second);
    it = impl_->bodies.erase(it);
  }
  std::vector<Node*> nodes;
  collect_physics_nodes(scene.roots(), nodes);
  for (Node* node : nodes) {
    if (!node->pending_destroy() && !impl_->bodies.contains(node->id())) impl_->add_node(node);
  }

  for (auto& [node_id, body] : impl_->bodies) {
    if (!b2Body_IsValid(body)) continue;
    const b2BodyType type = b2Body_GetType(body);
    if (type == b2_dynamicBody || type == b2_kinematicBody) {
      Node* node = scene.find_node(node_id);
      auto* rigid = node ? dynamic_cast<RigidBody2D*>(node->find_component("RigidBody2D")) : nullptr;
      if (rigid) {
        // Box2D recycles resting contacts without calling pre-solve; updating mass invalidates the cached
        // manifolds (and wakes the body) so the one-way test runs again while dropping through.
        if (rigid->drop_through_time > 0.0f) {
          b2Body_UpdateMassFromShapes(body);
          b2Body_SetAwake(body, true);
        }
        rigid->drop_through_time = std::max(0.0f, rigid->drop_through_time - fixed_dt);
        b2Body_SetLinearVelocity(body, px_to_m(rigid->linear_velocity));
        b2Body_SetGravityScale(body, rigid->gravity_scale);
        if (type == b2_dynamicBody && (rigid->pending_impulse.x != 0.0f || rigid->pending_impulse.y != 0.0f)) {
          b2Body_ApplyLinearImpulseToCenter(body, px_to_m(rigid->pending_impulse), true);
        }
        rigid->pending_impulse = {};
        if (rigid->has_pending_teleport) {
          b2Body_SetTransform(body, px_to_m(rigid->pending_teleport), b2MakeRot(-node->world_transform().rotation_radians));
          b2Body_SetLinearVelocity(body, b2Vec2{0.0f, 0.0f});
          b2Body_SetAwake(body, true);
          rigid->has_pending_teleport = false;
        }
      }
    }
  }

  b2World_Step(impl_->world, fixed_dt, 4);

  for (auto& [node_id, body] : impl_->bodies) {
    if (!b2Body_IsValid(body)) continue;
    const b2BodyType type = b2Body_GetType(body);
    if (type != b2_dynamicBody && type != b2_kinematicBody) continue;

    Node* node = scene.find_node(node_id);
    if (!node) continue;

    node->local_transform().position = m_to_px(b2Body_GetPosition(body));
    node->local_transform().rotation_radians = -b2Rot_GetAngle(b2Body_GetRotation(body));

    auto* rigid = dynamic_cast<RigidBody2D*>(node->find_component("RigidBody2D"));
    if (rigid) {
      rigid->linear_velocity = m_to_px(b2Body_GetLinearVelocity(body));
      rigid->grounded = is_grounded(body);
    }
  }

  const b2ContactEvents events = b2World_GetContactEvents(impl_->world);
  for (int i = 0; i < events.beginCount; ++i) {
    notify_contact(scene, events.beginEvents[i].shapeIdA, events.beginEvents[i].shapeIdB, /*begin=*/true);
  }
  for (int i = 0; i < events.endCount; ++i) {
    notify_contact(scene, events.endEvents[i].shapeIdA, events.endEvents[i].shapeIdB, /*begin=*/false);
  }
}

}  // namespace arcane
