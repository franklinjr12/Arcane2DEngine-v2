// ENG-074 AttackHitbox: contact/swing windows, knockback, layer masks, follow_parent, projectile. Headless, no gravity.
#include <cmath>
#include <memory>

#include "arcane/combat.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

constexpr float kDt = 1.0f / 60.0f;
constexpr int kTargetLayer = 2;
constexpr int kOtherLayer = 3;
constexpr int kAttackLayer = 5;

ComponentRegistry make_registry() {
  ComponentRegistry registry;
  register_physics_components(registry);
  return registry;
}

template <typename T>
T* add(Scene& scene, Node* node, const char* type, const ComponentRegistry& registry) {
  return static_cast<T*>(scene.add_component(node, type, registry));
}

void run(Scene& scene, PhysicsWorld& world, int steps) {
  for (int i = 0; i < steps; ++i) {
    scene.update(kDt);
    world.step(scene, kDt);
  }
}

struct Target {
  Node* node;
  Health* health;
  RigidBody2D* body;
};

// A solid Dynamic, gravity-free body with Health + Hurtbox.
Target add_target(Scene& scene, const ComponentRegistry& registry, Vec2 position, float invulnerability, int layer = kTargetLayer) {
  Target t;
  t.node = scene.create_node("Target");
  t.node->local_transform().position = position;
  t.body = add<RigidBody2D>(scene, t.node, "RigidBody2D", registry);
  t.body->gravity_scale = 0.0f;
  auto* collider = add<Collider2D>(scene, t.node, "Collider2D", registry);
  collider->layer = layer;
  t.health = add<Health>(scene, t.node, "Health", registry);
  t.health->max_health = 100.0f;
  t.health->invulnerability_time = invulnerability;
  add<Hurtbox>(scene, t.node, "Hurtbox", registry);
  return t;
}

struct Attacker {
  Node* root;
  Node* attack;
  RigidBody2D* root_body;
  AttackHitbox* hitbox;
  Collider2D* sensor;
};

Attacker add_attacker(Scene& scene, const ComponentRegistry& registry, Vec2 position, int active_steps) {
  Attacker a;
  a.root = scene.create_node("Attacker");
  a.root->local_transform().position = position;
  a.root_body = add<RigidBody2D>(scene, a.root, "RigidBody2D", registry);
  a.root_body->body_type = static_cast<int>(BodyType::Kinematic);
  a.root_body->gravity_scale = 0.0f;
  a.attack = scene.create_node("Attack", a.root);
  auto* body = add<RigidBody2D>(scene, a.attack, "RigidBody2D", registry);
  body->body_type = static_cast<int>(BodyType::Kinematic);
  body->gravity_scale = 0.0f;
  a.sensor = add<Collider2D>(scene, a.attack, "Collider2D", registry);
  a.sensor->is_sensor = true;
  a.sensor->layer = kAttackLayer;
  a.sensor->mask = 1 << kTargetLayer;
  a.hitbox = add<AttackHitbox>(scene, a.attack, "AttackHitbox", registry);
  a.hitbox->active_steps = active_steps;
  return a;
}

void test_contact_damages_once_per_iframe_window() {
  auto registry = make_registry();
  Scene scene;
  Attacker a = add_attacker(scene, registry, {0.0f, 0.0f}, 0);
  Target t = add_target(scene, registry, {10.0f, 0.0f}, 0.5f);
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 70);
  ARCANE_ASSERT_TRUE(t.health->current() == 97.0f);
  ARCANE_ASSERT_TRUE(a.hitbox->hits_dealt() == 3);
  ARCANE_ASSERT_TRUE(a.hitbox->active_now());
  ARCANE_ASSERT_TRUE(t.health->last_event()->source == a.root->id());
}

void test_swing_active_only_in_window() {
  auto registry = make_registry();
  Scene scene;
  Attacker a = add_attacker(scene, registry, {0.0f, 0.0f}, 6);
  Target t = add_target(scene, registry, {10.0f, 0.0f}, 0.0f);
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 10);
  ARCANE_ASSERT_TRUE(t.health->current() == 100.0f && !a.hitbox->active_now());
  a.hitbox->activate();
  ARCANE_ASSERT_TRUE(a.hitbox->active_now() && a.hitbox->activation_count() == 1);
  run(scene, world, 5);
  ARCANE_ASSERT_TRUE(a.hitbox->active_now());
  run(scene, world, 1);
  ARCANE_ASSERT_TRUE(!a.hitbox->active_now());
  ARCANE_ASSERT_TRUE(t.health->current() == 99.0f && a.hitbox->hits_dealt() == 1);
  run(scene, world, 10);
  ARCANE_ASSERT_TRUE(t.health->current() == 99.0f);
  a.hitbox->activate();
  run(scene, world, 8);
  ARCANE_ASSERT_TRUE(t.health->current() == 98.0f && a.hitbox->hits_dealt() == 2 && a.hitbox->activation_count() == 2);
}

void test_knockback_moves_target_away() {
  for (const float side : {1.0f, -1.0f}) {
    auto registry = make_registry();
    Scene scene;
    Attacker a = add_attacker(scene, registry, {0.0f, 0.0f}, 4);
    a.hitbox->knockback = 50.0f;
    Target t = add_target(scene, registry, {10.0f * side, 0.0f}, 0.0f);
    PhysicsWorld world;
    world.rebuild(scene);
    run(scene, world, 2);
    a.hitbox->activate();
    run(scene, world, 5);
    ARCANE_ASSERT_TRUE(t.health->current() == 99.0f);
    ARCANE_ASSERT_TRUE(t.body->linear_velocity.x * side > 0.0f);
    ARCANE_ASSERT_TRUE((t.node->local_transform().position.x - 10.0f * side) * side > 0.0f);
  }
}

void test_layer_masks_respected() {
  auto registry = make_registry();
  Scene scene;
  Attacker a = add_attacker(scene, registry, {0.0f, 0.0f}, 0);
  // The attacker's own parent has a Hurtbox + Health: never damaged.
  auto* own_health = add<Health>(scene, a.root, "Health", registry);
  add<Hurtbox>(scene, a.root, "Hurtbox", registry);
  Target masked_out = add_target(scene, registry, {10.0f, 0.0f}, 0.0f, kOtherLayer);
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 20);
  ARCANE_ASSERT_TRUE(masked_out.health->current() == 100.0f && a.hitbox->hits_dealt() == 0);
  ARCANE_ASSERT_TRUE(own_health->current() == own_health->max_health);

  // Target whose own mask excludes the attack layer is not hit either.
  Scene scene2;
  Attacker b = add_attacker(scene2, registry, {0.0f, 0.0f}, 0);
  Target deaf = add_target(scene2, registry, {10.0f, 0.0f}, 0.0f);
  static_cast<Collider2D*>(deaf.node->find_component("Collider2D"))->mask = 1 << 0;
  PhysicsWorld world2;
  world2.rebuild(scene2);
  run(scene2, world2, 20);
  ARCANE_ASSERT_TRUE(deaf.health->current() == 100.0f && b.hitbox->hits_dealt() == 0);

  Scene scene3;
  Attacker c = add_attacker(scene3, registry, {0.0f, 0.0f}, 0);
  Target ok = add_target(scene3, registry, {10.0f, 0.0f}, 0.0f);
  PhysicsWorld world3;
  world3.rebuild(scene3);
  run(scene3, world3, 5);
  ARCANE_ASSERT_TRUE(ok.health->current() < 100.0f && c.hitbox->hits_dealt() >= 1);
}

void test_follow_parent() {
  auto registry = make_registry();
  Scene scene;
  Attacker a = add_attacker(scene, registry, {0.0f, 0.0f}, 0);
  a.hitbox->offset = {20.0f, 0.0f};
  a.root_body->set_velocity({60.0f, 0.0f});
  Target t = add_target(scene, registry, {65.0f, 0.0f}, 0.0f);
  t.body->body_type = static_cast<int>(BodyType::Static);
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 30);
  scene.update(kDt);  // the follow queues a teleport of the child body to parent world + offset
  const Vec2 parent = a.root->world_transform().position;
  const auto* child_body = static_cast<RigidBody2D*>(a.attack->find_component("RigidBody2D"));
  ARCANE_ASSERT_TRUE(child_body->has_pending_teleport);
  const Vec2 child = child_body->pending_teleport;
  ARCANE_ASSERT_TRUE(parent.x > 25.0f);
  ARCANE_ASSERT_TRUE(std::fabs(child.x - (parent.x + 20.0f)) < 1.5f);
  ARCANE_ASSERT_TRUE(t.health->current() < 100.0f);
}

void test_projectile_sibling() {
  auto registry = make_registry();
  Scene scene;
  Target t = add_target(scene, registry, {100.0f, 0.0f}, 0.5f);
  Node* shot = scene.create_node("Shot");
  auto* body = add<RigidBody2D>(scene, shot, "RigidBody2D", registry);
  body->gravity_scale = 0.0f;
  auto* sensor = add<Collider2D>(scene, shot, "Collider2D", registry);
  sensor->width = sensor->height = 8.0f;
  sensor->is_sensor = true;
  sensor->layer = kAttackLayer;
  sensor->mask = 1 << kTargetLayer;
  auto* hit = add<AttackHitbox>(scene, shot, "AttackHitbox", registry);
  hit->damage = 4.0f;
  add<Projectile>(scene, shot, "Projectile", registry);
  const NodeId id = shot->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 60);
  ARCANE_ASSERT_TRUE(t.health->current() == 96.0f);
  ARCANE_ASSERT_TRUE(t.health->last_event()->source == id);
  ARCANE_ASSERT_TRUE(!scene.find_node(id));
}

}  // namespace

int main() {
  test_contact_damages_once_per_iframe_window();
  test_swing_active_only_in_window();
  test_knockback_moves_target_away();
  test_layer_masks_respected();
  test_follow_parent();
  test_projectile_sibling();
  return 0;
}
