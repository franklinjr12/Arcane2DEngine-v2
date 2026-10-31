#include <cmath>
#include <memory>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/topdown.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct World {
  arcane::ComponentRegistry registry;
  arcane::Scene scene;
  arcane::PhysicsWorld physics;
  arcane::InputActionMap input;
  arcane::FakeInputBackend keys;
  arcane::Node* player = nullptr;
  arcane::RigidBody2D* body = nullptr;
  arcane::TopDownController2D* controller = nullptr;

  explicit World(bool with_body = true, bool with_input = true) {
    arcane::register_builtin_components(registry);
    arcane::register_physics_components(registry);
    player = scene.create_node("player");
    if (with_body) {
      body = static_cast<arcane::RigidBody2D*>(scene.add_component(player, "RigidBody2D", registry));
      body->gravity_scale = 0.0f;
      body->fixed_rotation = true;
      auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(player, "Collider2D", registry));
      collider->width = 16.0f;
      collider->height = 16.0f;
    }
    controller = static_cast<arcane::TopDownController2D*>(scene.add_component(player, "TopDownController2D", registry));
    input.set_action({"left", {{arcane::InputDevice::Keyboard, arcane::Keys::kA}}});
    input.set_action({"right", {{arcane::InputDevice::Keyboard, arcane::Keys::kD}}});
    input.set_action({"up", {{arcane::InputDevice::Keyboard, arcane::Keys::kW}}});
    input.set_action({"down", {{arcane::InputDevice::Keyboard, arcane::Keys::kS}}});
    if (with_input) arcane::set_active_input_map(&input);
    physics.rebuild(scene);
  }
  ~World() { arcane::set_active_input_map(nullptr); }

  void step(int n = 1) {
    for (int i = 0; i < n; ++i) {
      input.update(keys);
      scene.update(kDt);
      physics.step(scene, kDt);
    }
  }
  void key(int code, bool down) { keys.set_down(arcane::InputDevice::Keyboard, code, down); }
  float speed() const { return std::hypot(body->linear_velocity.x, body->linear_velocity.y); }
};

bool near(float a, float b, float eps = 1.0f) { return std::fabs(a - b) <= eps; }

void test_right_reaches_max_speed() {
  World w;
  w.key(arcane::Keys::kD, true);
  w.step(60);
  ARCANE_ASSERT_TRUE(near(w.body->linear_velocity.x, 160.0f));
  ARCANE_ASSERT_TRUE(near(w.body->linear_velocity.y, 0.0f));
  ARCANE_ASSERT_TRUE(w.controller->moving());
}

void test_diagonal_normalized() {
  World w;
  w.key(arcane::Keys::kD, true);
  w.key(arcane::Keys::kS, true);
  w.step(60);
  ARCANE_ASSERT_TRUE(near(w.speed(), 160.0f));
  ARCANE_ASSERT_TRUE(w.body->linear_velocity.y > 100.0f);

  World raw;
  raw.controller->normalize_diagonal = false;
  raw.key(arcane::Keys::kD, true);
  raw.key(arcane::Keys::kS, true);
  raw.step(60);
  ARCANE_ASSERT_TRUE(raw.speed() > 200.0f);
}

void test_release_decelerates_and_keeps_facing() {
  World w;
  w.key(arcane::Keys::kA, true);
  w.step(60);
  w.key(arcane::Keys::kA, false);
  w.step(60);
  ARCANE_ASSERT_TRUE(near(w.speed(), 0.0f, 0.01f));
  ARCANE_ASSERT_TRUE(!w.controller->moving());
  ARCANE_ASSERT_TRUE(near(w.controller->facing().x, -1.0f, 0.001f) && near(w.controller->facing().y, 0.0f, 0.001f));
}

void test_flip_sprite() {
  World w;
  auto* sprite = static_cast<arcane::Sprite*>(w.scene.add_component(w.player, "Sprite", w.registry));
  w.controller->flip_sprite = true;
  w.key(arcane::Keys::kA, true);
  w.step(2);
  ARCANE_ASSERT_TRUE(sprite->flip_x);
  w.key(arcane::Keys::kA, false);
  w.key(arcane::Keys::kD, true);
  w.step(2);
  ARCANE_ASSERT_TRUE(!sprite->flip_x);

  World off;
  auto* other = static_cast<arcane::Sprite*>(off.scene.add_component(off.player, "Sprite", off.registry));
  off.key(arcane::Keys::kA, true);
  off.step(2);
  ARCANE_ASSERT_TRUE(!other->flip_x);
}

void test_noop_without_body_or_input() {
  {
    World no_body(false, true);
    no_body.key(arcane::Keys::kD, true);
    no_body.step(5);
    ARCANE_ASSERT_TRUE(no_body.body == nullptr);
  }

  World no_input(true, false);
  no_input.key(arcane::Keys::kD, true);
  no_input.step(5);
  ARCANE_ASSERT_TRUE(no_input.body->linear_velocity.x == 0.0f);
}

void test_serialization_round_trip() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  arcane::Scene original;
  arcane::Node* node = original.create_node("p");
  auto* c = static_cast<arcane::TopDownController2D*>(original.add_component(node, "TopDownController2D", registry));
  c->max_speed = 90.0f;
  c->acceleration = 500.0f;
  c->deceleration = 700.0f;
  c->normalize_diagonal = false;
  c->flip_sprite = true;
  c->left_action = "west";
  c->right_action = "east";
  c->up_action = "north";
  c->down_action = "south";
  auto json = original.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(json.has_value());
  auto loaded = arcane::Scene::load_from_json_strict(*json, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  auto* r = static_cast<arcane::TopDownController2D*>(loaded->roots()[0]->find_component("TopDownController2D"));
  ARCANE_ASSERT_TRUE(r != nullptr);
  ARCANE_ASSERT_TRUE(r->max_speed == 90.0f && r->acceleration == 500.0f && r->deceleration == 700.0f);
  ARCANE_ASSERT_TRUE(!r->normalize_diagonal && r->flip_sprite);
  ARCANE_ASSERT_TRUE(r->left_action == "west" && r->right_action == "east");
  ARCANE_ASSERT_TRUE(r->up_action == "north" && r->down_action == "south");
}

}  // namespace

int main() {
  test_right_reaches_max_speed();
  test_diagonal_normalized();
  test_release_decelerates_and_keeps_facing();
  test_flip_sprite();
  test_noop_without_body_or_input();
  test_serialization_round_trip();
  return 0;
}
