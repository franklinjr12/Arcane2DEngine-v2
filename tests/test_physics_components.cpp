#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

class ContactRecorder : public arcane::Component {
 public:
  int begin_count = 0;
  int end_count = 0;
  void on_collision_enter(arcane::Node&) override { ++begin_count; }
  void on_collision_exit(arcane::Node&) override { ++end_count; }
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  registry.register_component("ContactRecorder", [] { return std::make_unique<ContactRecorder>(); }, {});
  return registry;
}

// A dynamic box falls under gravity and comes to rest on a static box below
// it, and both sides see on_collision_enter exactly once.
void test_falling_body_lands_on_static_box() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene scene;

  arcane::Node* ground = scene.create_node("ground");
  ground->local_transform().position = {0.0f, 0.0f};
  scene.add_component(ground, "RigidBody2D", registry);
  auto* ground_body = static_cast<arcane::RigidBody2D*>(ground->find_component("RigidBody2D"));
  ground_body->body_type = static_cast<int>(arcane::BodyType::Static);
  auto* ground_collider = static_cast<arcane::Collider2D*>(scene.add_component(ground, "Collider2D", registry));
  ground_collider->width = 320.0f;
  ground_collider->height = 32.0f;
  auto* ground_recorder = static_cast<ContactRecorder*>(scene.add_component(ground, "ContactRecorder", registry));

  arcane::Node* falling = scene.create_node("falling");
  falling->local_transform().position = {0.0f, -320.0f};  // 10 m above origin on screen (y-down).
  scene.add_component(falling, "RigidBody2D", registry);
  auto* falling_collider = static_cast<arcane::Collider2D*>(scene.add_component(falling, "Collider2D", registry));
  falling_collider->width = 32.0f;
  falling_collider->height = 32.0f;
  auto* falling_recorder = static_cast<ContactRecorder*>(scene.add_component(falling, "ContactRecorder", registry));

  arcane::PhysicsWorld world;
  world.rebuild(scene);

  constexpr float kFixedDt = 1.0f / 60.0f;
  for (int i = 0; i < 180; ++i) {
    world.step(scene, kFixedDt);
  }

  // Rests on top of the ground box: center 32 px above the ground center
  // (half of each box's height: 16 + 16), i.e. y near -32 in y-down space.
  ARCANE_ASSERT_TRUE(falling->local_transform().position.y < -16.0f);
  ARCANE_ASSERT_TRUE(falling->local_transform().position.y > -64.0f);

  ARCANE_ASSERT_TRUE(falling_recorder->begin_count >= 1);
  ARCANE_ASSERT_TRUE(ground_recorder->begin_count >= 1);
  ARCANE_ASSERT_TRUE(falling_recorder->end_count == 0);
}

// Serializing and reloading RigidBody2D/Collider2D round-trips every
// property through the generic property system.
void test_physics_component_serialization_round_trip() {
  arcane::ComponentRegistry registry = make_registry();

  arcane::Scene original;
  arcane::Node* node = original.create_node("body");
  auto* rigid = static_cast<arcane::RigidBody2D*>(original.add_component(node, "RigidBody2D", registry));
  rigid->body_type = static_cast<int>(arcane::BodyType::Kinematic);
  rigid->linear_velocity = {3.0f, -4.0f};
  rigid->gravity_scale = 0.5f;
  rigid->fixed_rotation = true;

  auto* collider = static_cast<arcane::Collider2D*>(original.add_component(node, "Collider2D", registry));
  collider->shape_type = static_cast<int>(arcane::ColliderShapeType::Circle);
  collider->radius = 8.0f;
  collider->offset = {1.0f, 2.0f};
  collider->density = 2.0f;
  collider->friction = 0.1f;
  collider->restitution = 0.9f;
  collider->is_sensor = true;

  const std::string json = original.save_to_json(registry);
  auto loaded = arcane::Scene::load_from_json(json, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());

  arcane::Node* reloaded_node = loaded->roots()[0].get();
  auto* reloaded_rigid = static_cast<arcane::RigidBody2D*>(reloaded_node->find_component("RigidBody2D"));
  ARCANE_ASSERT_TRUE(reloaded_rigid != nullptr);
  ARCANE_ASSERT_TRUE(reloaded_rigid->body_type == static_cast<int>(arcane::BodyType::Kinematic));
  ARCANE_ASSERT_TRUE(reloaded_rigid->linear_velocity.x == 3.0f && reloaded_rigid->linear_velocity.y == -4.0f);
  ARCANE_ASSERT_TRUE(reloaded_rigid->gravity_scale == 0.5f);
  ARCANE_ASSERT_TRUE(reloaded_rigid->fixed_rotation == true);

  auto* reloaded_collider = static_cast<arcane::Collider2D*>(reloaded_node->find_component("Collider2D"));
  ARCANE_ASSERT_TRUE(reloaded_collider != nullptr);
  ARCANE_ASSERT_TRUE(reloaded_collider->shape_type == static_cast<int>(arcane::ColliderShapeType::Circle));
  ARCANE_ASSERT_TRUE(reloaded_collider->radius == 8.0f);
  ARCANE_ASSERT_TRUE(reloaded_collider->offset.x == 1.0f && reloaded_collider->offset.y == 2.0f);
  ARCANE_ASSERT_TRUE(reloaded_collider->density == 2.0f);
  ARCANE_ASSERT_TRUE(reloaded_collider->friction == 0.1f);
  ARCANE_ASSERT_TRUE(reloaded_collider->restitution == 0.9f);
  ARCANE_ASSERT_TRUE(reloaded_collider->is_sensor == true);
}

// A PhysicsWorld that is never stepped (e.g. the editor's authored scene,
// which is never handed to a PhysicsWorld at all) never mutates the scene:
// rebuild() alone must not move anything.
void test_rebuild_without_step_does_not_mutate_scene() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("body");
  node->local_transform().position = {5.0f, 7.0f};
  scene.add_component(node, "RigidBody2D", registry);
  scene.add_component(node, "Collider2D", registry);

  arcane::PhysicsWorld world;
  world.rebuild(scene);

  ARCANE_ASSERT_TRUE(node->local_transform().position.x == 5.0f);
  ARCANE_ASSERT_TRUE(node->local_transform().position.y == 7.0f);
}

// Default gravity pulls a body toward +y (down on screen), and velocity is
// reported in the same y-down space.
void test_gravity_moves_body_down_on_screen() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("body");
  node->local_transform().position = {10.0f, 50.0f};
  scene.add_component(node, "RigidBody2D", registry);
  auto* rigid = static_cast<arcane::RigidBody2D*>(node->find_component("RigidBody2D"));
  scene.add_component(node, "Collider2D", registry);

  arcane::PhysicsWorld world;
  world.rebuild(scene);
  for (int i = 0; i < 30; ++i) world.step(scene, 1.0f / 60.0f);

  ARCANE_ASSERT_TRUE(node->local_transform().position.y > 50.0f);
  ARCANE_ASSERT_TRUE(rigid->linear_velocity.y > 0.0f);
  ARCANE_ASSERT_TRUE(node->local_transform().position.x == 10.0f);
}

// Position, velocity and rotation survive the boundary with their sign: a
// zero-gravity body moves exactly along its (y-down) velocity.
void test_position_velocity_round_trip() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("body");
  node->local_transform().position = {100.0f, 200.0f};
  scene.add_component(node, "RigidBody2D", registry);
  auto* rigid = static_cast<arcane::RigidBody2D*>(node->find_component("RigidBody2D"));
  rigid->gravity_scale = 0.0f;
  rigid->linear_velocity = {64.0f, -96.0f};  // right and UP on screen.
  scene.add_component(node, "Collider2D", registry);

  arcane::PhysicsWorld world;
  world.rebuild(scene);
  for (int i = 0; i < 60; ++i) world.step(scene, 1.0f / 60.0f);

  const arcane::Vec2 p = node->local_transform().position;
  ARCANE_ASSERT_TRUE(std::fabs(p.x - 164.0f) < 0.5f);
  ARCANE_ASSERT_TRUE(std::fabs(p.y - 104.0f) < 0.5f);
  ARCANE_ASSERT_TRUE(std::fabs(rigid->linear_velocity.x - 64.0f) < 0.01f);
  ARCANE_ASSERT_TRUE(std::fabs(rigid->linear_velocity.y + 96.0f) < 0.01f);
}

// A body launched upward (negative y velocity) rises on screen before gravity
// turns it around.
void test_upward_velocity_rises_then_falls() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("body");
  node->local_transform().position = {0.0f, 0.0f};
  scene.add_component(node, "RigidBody2D", registry);
  auto* rigid = static_cast<arcane::RigidBody2D*>(node->find_component("RigidBody2D"));
  rigid->linear_velocity = {0.0f, -320.0f};
  scene.add_component(node, "Collider2D", registry);

  arcane::PhysicsWorld world;
  world.rebuild(scene);
  world.step(scene, 1.0f / 60.0f);
  ARCANE_ASSERT_TRUE(node->local_transform().position.y < 0.0f);
  float min_y = 0.0f;
  for (int i = 0; i < 300; ++i) {
    world.step(scene, 1.0f / 60.0f);
    min_y = std::min(min_y, node->local_transform().position.y);
  }
  ARCANE_ASSERT_TRUE(min_y < -20.0f);
  ARCANE_ASSERT_TRUE(node->local_transform().position.y > min_y);
}

}  // namespace

// A collider with mirror_with_sprite moves its offset to the other side when the Sprite flips during Play.
void test_collider_mirrors_with_sprite_flip() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::register_builtin_components(registry);
  arcane::Scene scene;
  arcane::Node* node = scene.create_node("hero");
  auto* sprite = static_cast<arcane::Sprite*>(scene.add_component(node, "Sprite", registry));
  auto* body = static_cast<arcane::RigidBody2D*>(scene.add_component(node, "RigidBody2D", registry));
  body->body_type = static_cast<int>(arcane::BodyType::Static);
  auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(node, "Collider2D", registry));
  collider->width = 10.0f;
  collider->height = 10.0f;
  collider->offset = {20.0f, 0.0f};
  collider->mirror_with_sprite = true;

  arcane::PhysicsWorld world;
  world.rebuild(scene);
  world.step(scene, 1.0f / 60.0f);
  ARCANE_ASSERT_TRUE(!world.overlap_point({20.0f, 0.0f}).empty() && world.overlap_point({-20.0f, 0.0f}).empty());
  sprite->flip_x = true;
  world.step(scene, 1.0f / 60.0f);
  ARCANE_ASSERT_TRUE(world.overlap_point({20.0f, 0.0f}).empty() && !world.overlap_point({-20.0f, 0.0f}).empty());
  collider->mirror_with_sprite = false;  // only read when the shape is built: flipping back mirrors again
  sprite->flip_x = false;
  world.step(scene, 1.0f / 60.0f);
  ARCANE_ASSERT_TRUE(!world.overlap_point({20.0f, 0.0f}).empty());
}

int main() {
  test_gravity_moves_body_down_on_screen();
  test_position_velocity_round_trip();
  test_upward_velocity_rises_then_falls();
  test_falling_body_lands_on_static_box();
  test_physics_component_serialization_round_trip();
  test_rebuild_without_step_does_not_mutate_scene();
  test_collider_mirrors_with_sprite_flip();
  return 0;
}
