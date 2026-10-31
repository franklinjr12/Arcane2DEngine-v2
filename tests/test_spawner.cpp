// GP5: runtime Node active flag (update, physics) and the pooled Spawner2D. Headless: no window.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/physics.hpp"
#include "arcane/prefab.hpp"
#include "arcane/scene.hpp"
#include "arcane/spawner.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr const char* kAsset = "prefabs/bullet.prefab.json";

class Counter : public arcane::Component {
 public:
  int starts = 0, updates = 0, reuses = 0, trigger_enters = 0, trigger_exits = 0;
  void on_start() override { ++starts; }
  void on_update(float) override { ++updates; }
  void on_pool_reuse() override { ++reuses; }
  void on_trigger_enter(arcane::Node&) override { ++trigger_enters; }
  void on_trigger_exit(arcane::Node&) override { ++trigger_exits; }
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  registry.register_component("Counter", [] { return std::make_unique<Counter>(); }, {});
  return registry;
}

Counter* counter(const arcane::Node* node) { return static_cast<Counter*>(node->find_component("Counter")); }

// A project dir holding a "Bullet" prefab: Counter + RigidBody2D (no gravity) + Collider2D, with one child.
std::filesystem::path make_project(const arcane::ComponentRegistry& registry) {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_spawner_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "prefabs");
  arcane::Scene scene;
  arcane::Node* bullet = scene.create_node("Bullet");
  scene.add_component(bullet, "Counter", registry);
  auto* body = static_cast<arcane::RigidBody2D*>(scene.add_component(bullet, "RigidBody2D", registry));
  body->gravity_scale = 0.0f;
  scene.add_component(bullet, "Collider2D", registry);
  arcane::Node* trail = scene.create_node("Trail", bullet);
  scene.add_component(trail, "Counter", registry);
  auto prefab = arcane::Prefab::from_node(*bullet, registry);
  ARCANE_ASSERT_TRUE(prefab.has_value());
  auto text = prefab->to_json(registry);
  ARCANE_ASSERT_TRUE(text.has_value());
  std::ofstream(dir / kAsset, std::ios::binary) << *text;
  return dir;
}

arcane::Spawner2D* add_spawner(arcane::Scene& scene, const arcane::ComponentRegistry& registry) {
  arcane::Node* node = scene.create_node("Spawner");
  auto* spawner = static_cast<arcane::Spawner2D*>(scene.add_component(node, "Spawner2D", registry));
  spawner->prefab = kAsset;
  return spawner;
}

void test_inactive_subtree_skips_update() {
  auto registry = make_registry();
  arcane::Scene scene;
  arcane::Node* parent = scene.create_node("Parent");
  arcane::Node* child = scene.create_node("Child", parent);
  scene.add_component(parent, "Counter", registry);
  scene.add_component(child, "Counter", registry);
  ARCANE_ASSERT_TRUE(child->active() && child->active_in_hierarchy());
  parent->set_active(false);
  ARCANE_ASSERT_TRUE(child->active() && !child->active_in_hierarchy());
  for (int i = 0; i < 3; ++i) scene.update(kDt);
  ARCANE_ASSERT_TRUE(counter(parent)->starts == 0 && counter(parent)->updates == 0);
  ARCANE_ASSERT_TRUE(counter(child)->starts == 0 && counter(child)->updates == 0);
  parent->set_active(true);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(counter(child)->starts == 1 && counter(child)->updates == 1);
}

void test_inactive_body_has_no_hits_or_triggers() {
  auto registry = make_registry();
  arcane::Scene scene;
  arcane::Node* sensor = scene.create_node("Sensor");
  sensor->local_transform().position = {100.0f, 0.0f};
  scene.add_component(sensor, "Counter", registry);
  static_cast<arcane::RigidBody2D*>(scene.add_component(sensor, "RigidBody2D", registry))->body_type =
      static_cast<int>(arcane::BodyType::Static);
  static_cast<arcane::Collider2D*>(scene.add_component(sensor, "Collider2D", registry))->is_sensor = true;
  arcane::Node* visitor = scene.create_node("Visitor");
  visitor->local_transform().position = {100.0f, 0.0f};
  scene.add_component(visitor, "Counter", registry);
  static_cast<arcane::RigidBody2D*>(scene.add_component(visitor, "RigidBody2D", registry))->gravity_scale = 0.0f;
  scene.add_component(visitor, "Collider2D", registry);

  visitor->set_active(false);
  arcane::PhysicsWorld physics;
  physics.rebuild(scene);
  ARCANE_ASSERT_TRUE(!physics.raycast({0.0f, 0.0f}, {200.0f, 0.0f}));  // the sensor is never hit
  ARCANE_ASSERT_TRUE(physics.overlap_point({100.0f, 0.0f}).empty());
  for (int i = 0; i < 5; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
  }
  ARCANE_ASSERT_TRUE(counter(sensor)->trigger_enters == 0 && counter(visitor)->trigger_enters == 0);
  ARCANE_ASSERT_TRUE(physics.overlap_aabb({90.0f, -10.0f}, {110.0f, 10.0f}).empty());

  visitor->set_active(true);
  for (int i = 0; i < 2; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
  }
  auto hit = physics.raycast({0.0f, 0.0f}, {200.0f, 0.0f});
  ARCANE_ASSERT_TRUE(hit && hit->node == visitor->id());
  ARCANE_ASSERT_TRUE(physics.overlap_point({100.0f, 0.0f}).size() == 1);
  ARCANE_ASSERT_TRUE(counter(sensor)->trigger_enters == 1 && counter(visitor)->trigger_enters == 1);

  // Deactivating again: no hits and no exit event from the inactive node.
  visitor->set_active(false);
  for (int i = 0; i < 2; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
  }
  ARCANE_ASSERT_TRUE(!physics.raycast({0.0f, 0.0f}, {200.0f, 0.0f}));
  ARCANE_ASSERT_TRUE(counter(sensor)->trigger_exits == 0);
}

void test_prewarm_creates_inactive_instances() {
  auto registry = make_registry();
  const auto dir = make_project(registry);
  arcane::GameContext context(dir, registry);
  arcane::set_active_game_context(&context);
  arcane::Scene scene;
  auto* spawner = add_spawner(scene, registry);
  spawner->prewarm = 3;
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(spawner->free_count() == 3 && spawner->alive_count() == 0);
  int parked = 0;
  for (const auto& root : scene.roots()) {
    if (root->name() == "Bullet") {
      ARCANE_ASSERT_TRUE(!root->active() && !root->children().front()->active_in_hierarchy());
      ++parked;
    }
  }
  ARCANE_ASSERT_TRUE(parked == 3);
  scene.update(kDt);
  for (const auto& root : scene.roots()) {
    if (root->name() == "Bullet") ARCANE_ASSERT_TRUE(counter(root.get())->updates == 0);
  }
  arcane::set_active_game_context(nullptr);
}

void test_spawn_reuses_released_node() {
  auto registry = make_registry();
  const auto dir = make_project(registry);
  arcane::GameContext context(dir, registry);
  arcane::set_active_game_context(&context);
  arcane::Scene scene;
  auto* spawner = add_spawner(scene, registry);
  scene.update(kDt);

  arcane::Node* first = spawner->spawn({40.0f, 50.0f});
  ARCANE_ASSERT_TRUE(first && first->active() && first->parent() == nullptr);
  ARCANE_ASSERT_TRUE(first->local_transform().position.x == 40.0f && first->local_transform().position.y == 50.0f);
  ARCANE_ASSERT_TRUE(counter(first)->reuses == 0);
  const arcane::NodeId id = first->id();
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(counter(first)->updates == 1);

  ARCANE_ASSERT_TRUE(arcane::Spawner2D::release_to_pool(*first));
  ARCANE_ASSERT_TRUE(!first->active() && spawner->alive_count() == 0 && spawner->free_count() == 1);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(counter(first)->updates == 1);

  arcane::Node* again = spawner->spawn({7.0f, 8.0f});
  ARCANE_ASSERT_TRUE(again && again->id() == id && again->active());
  ARCANE_ASSERT_TRUE(again->local_transform().position.x == 7.0f);
  auto* body = static_cast<arcane::RigidBody2D*>(again->find_component("RigidBody2D"));
  ARCANE_ASSERT_TRUE(body->has_pending_teleport && body->pending_teleport.y == 8.0f);
  ARCANE_ASSERT_TRUE(counter(again)->reuses == 1 && counter(again->children().front().get())->reuses == 1);

  // An instance destroyed by other code leaves the pool.
  scene.destroy_node(id);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(spawner->spawn({0.0f, 0.0f}) != nullptr);
  ARCANE_ASSERT_TRUE(spawner->alive_count() == 1 && spawner->free_count() == 0);

  // Destroying the spawner destroys its instances.
  scene.destroy_node(spawner->node()->id());
  scene.update(kDt);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(!scene.find_node_by_name("Bullet"));
  arcane::set_active_game_context(nullptr);
}

void test_max_alive_caps_spawns() {
  auto registry = make_registry();
  const auto dir = make_project(registry);
  arcane::GameContext context(dir, registry);
  arcane::set_active_game_context(&context);
  arcane::Scene scene;
  auto* spawner = add_spawner(scene, registry);
  spawner->max_alive = 2;
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(spawner->spawn({}) && spawner->spawn({}));
  ARCANE_ASSERT_TRUE(spawner->spawn({}) == nullptr);
  ARCANE_ASSERT_TRUE(spawner->alive_count() == 2);

  auto* broken = add_spawner(scene, registry);
  broken->prefab = "prefabs/missing.prefab.json";
  ARCANE_ASSERT_TRUE(broken->spawn({}) == nullptr && broken->spawn({}) == nullptr);
  arcane::set_active_game_context(nullptr);
}

void test_interval_spawns_over_time() {
  auto registry = make_registry();
  const auto dir = make_project(registry);
  arcane::GameContext context(dir, registry);
  arcane::set_active_game_context(&context);
  arcane::Scene scene;
  auto* spawner = add_spawner(scene, registry);
  spawner->interval = 0.25f;
  spawner->max_alive = 0;
  spawner->offset = {5.0f, 0.0f};
  spawner->node()->local_transform().position = {100.0f, 20.0f};
  for (int i = 0; i < 120; ++i) scene.update(kDt);  // 2 seconds
  ARCANE_ASSERT_TRUE(std::abs(spawner->alive_count() - 8) <= 1);
  arcane::Node* bullet = scene.find_node_by_name("Bullet");
  ARCANE_ASSERT_TRUE(bullet && bullet->local_transform().position.x == 105.0f);

  auto* manual = add_spawner(scene, registry);
  manual->interval = 0.25f;
  manual->auto_start = false;
  for (int i = 0; i < 60; ++i) scene.update(kDt);
  ARCANE_ASSERT_TRUE(manual->alive_count() == 0);
  arcane::set_active_game_context(nullptr);
}

void test_release_to_pool_rejects_plain_nodes() {
  auto registry = make_registry();
  arcane::Scene scene;
  arcane::Node* plain = scene.create_node("Plain");
  ARCANE_ASSERT_TRUE(!arcane::Spawner2D::release_to_pool(*plain));
  ARCANE_ASSERT_TRUE(plain->active());
}

void test_active_flag_is_not_saved() {
  auto registry = make_registry();
  arcane::Scene scene;
  scene.create_node("Off")->set_active(false);
  add_spawner(scene, registry);
  auto text = scene.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(text.has_value());
  ARCANE_ASSERT_TRUE(text->find("\"active\"") == std::string::npos);
  auto loaded = arcane::Scene::load_from_json(*text, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value() && loaded->find_node_by_name("Off")->active());
}

}  // namespace

int main() {
  test_inactive_subtree_skips_update();
  test_inactive_body_has_no_hits_or_triggers();
  test_prewarm_creates_inactive_instances();
  test_spawn_reuses_released_node();
  test_max_alive_caps_spawns();
  test_interval_spawns_over_time();
  test_release_to_pool_rejects_plain_nodes();
  test_active_flag_is_not_saved();
  return 0;
}
