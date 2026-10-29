#include <algorithm>
#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/platformer.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A tilemap floor (top edge at y=200) and a 16x16 player standing on it, driven by a fake keyboard.
struct World {
  arcane::ComponentRegistry registry;
  arcane::Scene scene;
  arcane::PhysicsWorld physics;
  arcane::InputActionMap input;
  arcane::FakeInputBackend keys;
  arcane::Node* player = nullptr;
  arcane::RigidBody2D* body = nullptr;
  arcane::PlatformerController2D* controller = nullptr;

  explicit World(float player_y = 191.0f, int floor_tiles = 40, bool one_way = false) {
    arcane::register_tilemap_components(registry);
    arcane::register_physics_components(registry);
    arcane::Node* ground = scene.create_node("ground");
    ground->local_transform().position = {0.0f, 200.0f};
    if (one_way) {
      auto* ground_body = static_cast<arcane::RigidBody2D*>(scene.add_component(ground, "RigidBody2D", registry));
      ground_body->body_type = static_cast<int>(arcane::BodyType::Static);
      ground->local_transform().position = {100.0f, 216.0f};
      auto* platform = static_cast<arcane::Collider2D*>(scene.add_component(ground, "Collider2D", registry));
      platform->width = 200.0f;
      platform->height = 32.0f;
      platform->one_way = true;
    } else {
    auto* map = static_cast<arcane::Tilemap*>(scene.add_component(ground, "Tilemap", registry));
    map->width = floor_tiles;
    map->height = 1;
    map->collide = true;
    map->solid_tiles = "1";
    map->add_layer();
    for (int x = 0; x < floor_tiles; ++x) map->set_tile(0, x, 0, 1);

    }

    player = scene.create_node("player");
    player->local_transform().position = {100.0f, player_y};
    body = static_cast<arcane::RigidBody2D*>(scene.add_component(player, "RigidBody2D", registry));
    body->fixed_rotation = true;
    auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(player, "Collider2D", registry));
    collider->width = 16.0f;
    collider->height = 16.0f;
    collider->friction = 0.0f;
    controller = static_cast<arcane::PlatformerController2D*>(scene.add_component(player, "PlatformerController2D", registry));

    input.set_action({"move_left", {{arcane::InputDevice::Keyboard, arcane::Keys::kA}}});
    input.set_action({"move_right", {{arcane::InputDevice::Keyboard, arcane::Keys::kD}}});
    input.set_action({"move_down", {{arcane::InputDevice::Keyboard, arcane::Keys::kS}}});
    input.set_action({"jump", {{arcane::InputDevice::Keyboard, arcane::Keys::kSpace}}});
    arcane::set_active_input_map(&input);
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
  float y() const { return player->local_transform().position.y; }
};

// Steps while holding jump (until `hold_steps`), returns the minimum y (apex, y-down) reached over `total` steps.
float jump_apex(float hold_seconds) {
  World w;
  w.step(30);
  w.key(arcane::Keys::kSpace, true);
  float apex = w.y();
  const int hold_steps = static_cast<int>(hold_seconds / kDt);
  for (int i = 0; i < 120; ++i) {
    if (i == hold_steps) w.key(arcane::Keys::kSpace, false);
    w.step();
    apex = std::min(apex, w.y());
  }
  return apex;
}

void test_stands_and_grounded() {
  World w;
  w.step(30);
  ARCANE_ASSERT_TRUE(w.body->grounded);
  ARCANE_ASSERT_TRUE(w.y() > 185.0f && w.y() < 195.0f);
}

void test_max_speed_respected() {
  World w;
  w.step(30);
  w.key(arcane::Keys::kD, true);
  float max_seen = 0.0f;
  for (int i = 0; i < 120; ++i) {
    w.step();
    max_seen = std::max(max_seen, w.body->linear_velocity.x);
  }
  ARCANE_ASSERT_TRUE(max_seen <= w.controller->max_speed + 0.01f);
  ARCANE_ASSERT_TRUE(max_seen > w.controller->max_speed - 1.0f);
  w.key(arcane::Keys::kD, false);
  w.step(60);
  ARCANE_ASSERT_TRUE(std::abs(w.body->linear_velocity.x) < 0.01f);
}

void test_jump_cut_lowers_apex() {
  const float full = jump_apex(10.0f);  // never released while rising
  const float cut = jump_apex(0.05f);
  ARCANE_ASSERT_TRUE(full < 191.0f - 20.0f);  // actually jumped
  ARCANE_ASSERT_TRUE(cut > full + 10.0f);      // released early -> noticeably lower
  ARCANE_ASSERT_TRUE(cut < 191.0f - 3.0f);
}

// Walks off a ledge (floor only 4 tiles wide); a press shortly after leaving jumps, a late press does not.
float press_after_ledge(int steps_after_leaving) {
  World w(191.0f, 4);  // tiles are 32 px? floor spans x in [0, 4*tile_size); player starts at x=100 and walks right
  w.step(30);
  w.key(arcane::Keys::kD, true);
  int guard = 0;
  while (w.body->grounded && guard++ < 600) w.step();
  ARCANE_ASSERT_TRUE(!w.body->grounded);
  w.step(steps_after_leaving);
  const float y_before = w.y();
  w.key(arcane::Keys::kSpace, true);
  float min_vy = 0.0f;
  for (int i = 0; i < 3; ++i) {
    w.step();
    min_vy = std::min(min_vy, w.body->linear_velocity.y);
  }
  (void)y_before;
  return min_vy;
}

void test_coyote_window() {
  ARCANE_ASSERT_TRUE(press_after_ledge(2) < -200.0f);   // inside 0.1 s window: jumped
  ARCANE_ASSERT_TRUE(press_after_ledge(20) > -100.0f);  // 0.33 s later: refused (still falling)
}

void test_buffered_jump_fires_on_landing() {
  World w(100.0f);  // drop from ~100 px above the floor
  w.step(5);
  ARCANE_ASSERT_TRUE(!w.body->grounded);
  // Press while falling, a few steps before landing; release immediately.
  int guard = 0;
  while (200.0f - 8.0f - w.y() > 20.0f && guard++ < 300) w.step();
  w.key(arcane::Keys::kSpace, true);
  w.step();
  w.key(arcane::Keys::kSpace, false);
  float min_vy = 0.0f;
  for (int i = 0; i < 40; ++i) {
    w.step();
    min_vy = std::min(min_vy, w.body->linear_velocity.y);
  }
  ARCANE_ASSERT_TRUE(min_vy < -100.0f);  // jumped on landing without a second press
}

void test_one_way() {
  World landing(100.0f, 40, true);
  landing.step(120);
  ARCANE_ASSERT_TRUE(landing.body->grounded);
  ARCANE_ASSERT_TRUE(landing.y() < 195.0f);
  landing.key(arcane::Keys::kS, true);
  landing.key(arcane::Keys::kSpace, true);
  landing.step();
  landing.step(59);
  ARCANE_ASSERT_TRUE(landing.y() > 240.0f);

  World rising(250.0f, 40, true);
  rising.body->linear_velocity.y = -400.0f;
  rising.step(12);
  ARCANE_ASSERT_TRUE(rising.y() < 200.0f);
  rising.step(120);
  ARCANE_ASSERT_TRUE(rising.body->grounded);
}

void test_serialization_round_trip() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  arcane::Scene original;
  arcane::Node* node = original.create_node("p");
  auto* c = static_cast<arcane::PlatformerController2D*>(original.add_component(node, "PlatformerController2D", registry));
  c->max_speed = 123.0f;
  c->coyote_time = 0.25f;
  c->jump_cut = 0.3f;
  c->jump_action = "hop";
  c->down_action = "descend";
  auto* collider = static_cast<arcane::Collider2D*>(original.add_component(node, "Collider2D", registry));
  collider->one_way = true;
  auto loaded = arcane::Scene::load_from_json(original.save_to_json(registry), registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  auto* restored = static_cast<arcane::PlatformerController2D*>(
      loaded->roots()[0]->find_component("PlatformerController2D"));
  ARCANE_ASSERT_TRUE(restored != nullptr);
  ARCANE_ASSERT_TRUE(restored->max_speed == 123.0f && restored->coyote_time == 0.25f);
  ARCANE_ASSERT_TRUE(restored->jump_cut == 0.3f && restored->jump_action == "hop");
  ARCANE_ASSERT_TRUE(restored->down_action == "descend");
  auto* restored_collider = static_cast<arcane::Collider2D*>(loaded->roots()[0]->find_component("Collider2D"));
  ARCANE_ASSERT_TRUE(restored_collider && restored_collider->one_way);
}

}  // namespace

int main() {
  test_stands_and_grounded();
  test_max_speed_respected();
  test_jump_cut_lowers_apex();
  test_coyote_window();
  test_buffered_jump_fires_on_landing();
  test_one_way();
  test_serialization_round_trip();
  return 0;
}
