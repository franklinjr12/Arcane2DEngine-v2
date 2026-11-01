// Trigger/collision events, collision layers and masks, and spatial queries (GP2).
#include <memory>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

class EventLog : public arcane::Component {
 public:
  std::vector<std::string> events;
  void on_collision_enter(arcane::Node& o) override { events.push_back("ce:" + o.name()); }
  void on_collision_exit(arcane::Node& o) override { events.push_back("cx:" + o.name()); }
  void on_trigger_enter(arcane::Node& o) override { events.push_back("te:" + o.name()); }
  void on_trigger_exit(arcane::Node& o) override { events.push_back("tx:" + o.name()); }
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  registry.register_component("EventLog", [] { return std::make_unique<EventLog>(); }, {});
  return registry;
}

struct Box {
  arcane::Node* node;
  arcane::RigidBody2D* body;
  arcane::Collider2D* collider;
  EventLog* log;
};

Box add_box(arcane::Scene& scene, const arcane::ComponentRegistry& registry, const std::string& name, arcane::Vec2 position,
            arcane::BodyType type, float size = 32.0f) {
  Box box;
  box.node = scene.create_node(name);
  box.node->local_transform().position = position;
  box.body = static_cast<arcane::RigidBody2D*>(scene.add_component(box.node, "RigidBody2D", registry));
  box.body->body_type = static_cast<int>(type);
  box.collider = static_cast<arcane::Collider2D*>(scene.add_component(box.node, "Collider2D", registry));
  box.collider->width = size;
  box.collider->height = size;
  box.log = static_cast<EventLog*>(scene.add_component(box.node, "EventLog", registry));
  return box;
}

void run(arcane::PhysicsWorld& world, arcane::Scene& scene, int steps) {
  for (int i = 0; i < steps; ++i) world.step(scene, 1.0f / 60.0f);
}

bool has(const Box& box, const std::string& event) {
  for (const auto& e : box.log->events) {
    if (e == event) return true;
  }
  return false;
}

// A body with no gravity moves through a static sensor: both nodes see trigger enter then exit, no collision events.
void test_trigger_enter_and_exit() {
  auto registry = make_registry();
  arcane::Scene scene;
  Box sensor = add_box(scene, registry, "zone", {0.0f, 0.0f}, arcane::BodyType::Static, 64.0f);
  sensor.collider->is_sensor = true;
  Box mover = add_box(scene, registry, "mover", {-200.0f, 0.0f}, arcane::BodyType::Dynamic);
  mover.body->gravity_scale = 0.0f;
  mover.body->set_velocity({200.0f, 0.0f});
  arcane::PhysicsWorld world;
  world.rebuild(scene);
  run(world, scene, 120);
  ARCANE_ASSERT_TRUE(has(sensor, "te:mover"));
  ARCANE_ASSERT_TRUE(has(sensor, "tx:mover"));
  ARCANE_ASSERT_TRUE(has(mover, "te:zone"));
  ARCANE_ASSERT_TRUE(has(mover, "tx:zone"));
  ARCANE_ASSERT_TRUE(mover.node->local_transform().position.x > 100.0f);  // not blocked
  for (const auto& e : mover.log->events) ARCANE_ASSERT_TRUE(e[0] == 't');
  ARCANE_ASSERT_TRUE(mover.log->events.size() == 2);
}

// A solid hit reports collision enter, and exit once the body is pushed away.
void test_collision_exit() {
  auto registry = make_registry();
  arcane::Scene scene;
  Box ground = add_box(scene, registry, "ground", {0.0f, 0.0f}, arcane::BodyType::Static, 64.0f);
  Box ball = add_box(scene, registry, "ball", {0.0f, -64.0f}, arcane::BodyType::Dynamic);
  arcane::PhysicsWorld world;
  world.rebuild(scene);
  run(world, scene, 60);
  ARCANE_ASSERT_TRUE(has(ground, "ce:ball"));
  ball.body->set_velocity({0.0f, -400.0f});
  run(world, scene, 10);
  ARCANE_ASSERT_TRUE(has(ground, "cx:ball"));
}

// Same inputs, same event order.
void test_event_order_is_deterministic() {
  std::vector<std::string> first;
  for (int round = 0; round < 2; ++round) {
    auto registry = make_registry();
    arcane::Scene scene;
    Box zone = add_box(scene, registry, "zone", {0.0f, 0.0f}, arcane::BodyType::Static, 200.0f);
    zone.collider->is_sensor = true;
    for (int i = 0; i < 4; ++i) {
      Box b = add_box(scene, registry, "b" + std::to_string(i), {-60.0f + 40.0f * static_cast<float>(i), 0.0f}, arcane::BodyType::Dynamic, 10.0f);
      b.body->gravity_scale = 0.0f;
    }
    arcane::PhysicsWorld world;
    world.rebuild(scene);
    run(world, scene, 3);
    auto* log = static_cast<EventLog*>(zone.node->find_component("EventLog"));
    if (round == 0) first = log->events;
    else ARCANE_ASSERT_TRUE(log->events == first);
    ARCANE_ASSERT_TRUE(log->events.size() == 4);
    ARCANE_ASSERT_TRUE(log->events[0] == "te:b0" && log->events[3] == "te:b3");
  }
}

// Masks: a body whose mask excludes the wall's layer falls through it; a body that includes it lands.
void test_layers_and_masks() {
  auto registry = make_registry();
  arcane::Scene scene;
  Box floor = add_box(scene, registry, "floor", {0.0f, 0.0f}, arcane::BodyType::Static, 400.0f);
  floor.collider->layer = 2;
  floor.collider->mask = 1 << 3;  // reacts to layer 3 only
  Box ghost = add_box(scene, registry, "ghost", {-100.0f, -300.0f}, arcane::BodyType::Dynamic);
  ghost.collider->layer = 4;
  ghost.collider->mask = 0x7FFFFFFF & ~(1 << 2);
  Box solid = add_box(scene, registry, "solid", {100.0f, -300.0f}, arcane::BodyType::Dynamic);
  solid.collider->layer = 3;
  arcane::PhysicsWorld world;
  world.rebuild(scene);
  run(world, scene, 180);
  ARCANE_ASSERT_TRUE(ghost.node->local_transform().position.y > 300.0f);   // fell through
  ARCANE_ASSERT_TRUE(solid.node->local_transform().position.y < -100.0f);  // rests on top (floor top is y=-200)
  ARCANE_ASSERT_TRUE(!has(floor, "ce:ghost"));
  ARCANE_ASSERT_TRUE(has(floor, "ce:solid"));
}

void test_queries() {
  auto registry = make_registry();
  arcane::Scene scene;
  Box wall = add_box(scene, registry, "wall", {100.0f, 0.0f}, arcane::BodyType::Static, 32.0f);
  wall.collider->layer = 1;
  Box far = add_box(scene, registry, "far", {200.0f, 0.0f}, arcane::BodyType::Static, 32.0f);
  Box zone = add_box(scene, registry, "zone", {50.0f, 0.0f}, arcane::BodyType::Static, 32.0f);
  zone.collider->is_sensor = true;
  arcane::PhysicsWorld world;
  world.rebuild(scene);
  run(world, scene, 1);

  auto hit = world.raycast({0.0f, 0.0f}, {300.0f, 0.0f});
  ARCANE_ASSERT_TRUE(hit.has_value());
  ARCANE_ASSERT_TRUE(hit->node == wall.node->id());  // closest, sensor ignored
  ARCANE_ASSERT_TRUE(std::abs(hit->point.x - 84.0f) < 0.5f);
  ARCANE_ASSERT_TRUE(std::abs(hit->distance - 84.0f) < 0.5f);
  ARCANE_ASSERT_TRUE(hit->normal.x < -0.9f);
  auto only_default = world.raycast({0.0f, 0.0f}, {300.0f, 0.0f}, 1 << 0);
  ARCANE_ASSERT_TRUE(only_default.has_value() && only_default->node == far.node->id());
  ARCANE_ASSERT_TRUE(!world.raycast({0.0f, 0.0f}, {300.0f, 0.0f}, 1 << 5).has_value());
  ARCANE_ASSERT_TRUE(!world.raycast({0.0f, 100.0f}, {300.0f, 100.0f}).has_value());

  auto box = world.overlap_aabb({90.0f, -10.0f}, {210.0f, 10.0f});
  ARCANE_ASSERT_TRUE(box.size() == 2 && box[0] == wall.node->id() && box[1] == far.node->id());
  ARCANE_ASSERT_TRUE(world.overlap_aabb({90.0f, -10.0f}, {210.0f, 10.0f}, 1 << 1).size() == 1);
  ARCANE_ASSERT_TRUE(world.overlap_aabb({0.0f, 50.0f}, {300.0f, 90.0f}).empty());
  auto point = world.overlap_point({200.0f, 5.0f});
  ARCANE_ASSERT_TRUE(point.size() == 1 && point[0] == far.node->id());
  ARCANE_ASSERT_TRUE(world.overlap_point({150.0f, 0.0f}).empty());
}

}  // namespace

int main() {
  test_trigger_enter_and_exit();
  test_collision_exit();
  test_event_order_is_deterministic();
  test_layers_and_masks();
  test_queries();
  return 0;
}
