// GP5 combat: Health, Hurtbox, Hitbox, Projectile. Headless: Scene + PhysicsWorld, no gravity, 1/60 s steps.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "arcane/combat.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/physics.hpp"
#include "arcane/prefab.hpp"
#include "arcane/scene.hpp"
#include "arcane/spawner.hpp"
#include "test_assert.hpp"

namespace {

using namespace arcane;

constexpr float kDt = 1.0f / 60.0f;
constexpr int kEnemyLayer = 2;
constexpr int kPlayerAttackLayer = 3;
constexpr int kWallLayer = 4;
constexpr const char* kAsset = "prefabs/shot.prefab.json";

ComponentRegistry make_registry() {
  ComponentRegistry registry;
  register_physics_components(registry);
  return registry;
}

template <typename T>
T* add(Scene& scene, Node* node, const char* type, const ComponentRegistry& registry) {
  return static_cast<T*>(scene.add_component(node, type, registry));
}

struct Target {
  Node* node;
  Health* health;
  Hurtbox* hurtbox;
};

// A solid static enemy (layer enemy, reacts to everything) with Health + Hurtbox.
Target add_enemy(Scene& scene, const ComponentRegistry& registry, Vec2 position, float max_health = 10.0f) {
  Target t;
  t.node = scene.create_node("Enemy");
  t.node->local_transform().position = position;
  add<RigidBody2D>(scene, t.node, "RigidBody2D", registry)->body_type = static_cast<int>(BodyType::Static);
  auto* collider = add<Collider2D>(scene, t.node, "Collider2D", registry);
  collider->layer = kEnemyLayer;
  t.health = add<Health>(scene, t.node, "Health", registry);
  t.health->max_health = max_health;
  t.health->invulnerability_time = 0.0f;
  t.hurtbox = add<Hurtbox>(scene, t.node, "Hurtbox", registry);
  return t;
}

struct Shot {
  Node* node;
  Projectile* projectile;
  Hitbox* hitbox;
  Collider2D* collider;
};

// A sensor projectile on layer player_attack that only reacts to `mask`; moves right at 300 px/s.
Shot add_shot(Scene& scene, const ComponentRegistry& registry, Vec2 position, int mask) {
  Shot s;
  s.node = scene.create_node("Shot");
  s.node->local_transform().position = position;
  add<RigidBody2D>(scene, s.node, "RigidBody2D", registry)->gravity_scale = 0.0f;
  s.collider = add<Collider2D>(scene, s.node, "Collider2D", registry);
  s.collider->width = s.collider->height = 8.0f;
  s.collider->is_sensor = true;
  s.collider->layer = kPlayerAttackLayer;
  s.collider->mask = mask;
  s.hitbox = add<Hitbox>(scene, s.node, "Hitbox", registry);
  s.hitbox->damage = 3.0f;
  s.projectile = add<Projectile>(scene, s.node, "Projectile", registry);
  return s;
}

void run(Scene& scene, PhysicsWorld& world, int steps) {
  for (int i = 0; i < steps; ++i) {
    scene.update(kDt);
    world.step(scene, kDt);
  }
}

void test_health() {
  auto registry = make_registry();
  Scene scene;
  Node* node = scene.create_node("Hero");
  auto* health = add<Health>(scene, node, "Health", registry);
  health->max_health = 10.0f;
  health->invulnerability_time = 0.5f;
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(health->current() == 10.0f && !health->dead());

  std::vector<HealthEvent> events;
  const int listener = health->add_listener([&](const HealthEvent& e) { events.push_back(e); });
  ARCANE_ASSERT_TRUE(health->apply_damage(4.0f, nullptr) == 4.0f && health->current() == 6.0f);
  ARCANE_ASSERT_TRUE(health->invulnerable());
  ARCANE_ASSERT_TRUE(health->apply_damage(4.0f, nullptr) == 0.0f && health->current() == 6.0f);  // i-frames
  ARCANE_ASSERT_TRUE(health->apply_damage(-1.0f, nullptr) == 0.0f);
  for (int i = 0; i < 31; ++i) scene.update(kDt);  // > 0.5 s
  ARCANE_ASSERT_TRUE(!health->invulnerable());
  ARCANE_ASSERT_TRUE(health->heal(100.0f) == 4.0f && health->current() == 10.0f);  // clamped
  ARCANE_ASSERT_TRUE(health->last_event() && health->last_event()->kind == HealthEvent::Kind::Healed);

  ARCANE_ASSERT_TRUE(health->apply_damage(50.0f, node) == 10.0f && health->dead());
  ARCANE_ASSERT_TRUE(health->last_event()->kind == HealthEvent::Kind::Died && health->last_event()->source == node->id());
  for (int i = 0; i < 40; ++i) scene.update(kDt);
  ARCANE_ASSERT_TRUE(health->apply_damage(1.0f, nullptr) == 0.0f && health->heal(1.0f) == 0.0f);
  health->kill();  // already dead: no second event
  int died = 0;
  for (const auto& e : events) died += e.kind == HealthEvent::Kind::Died;
  ARCANE_ASSERT_TRUE(died == 1 && events.size() == 3);  // Damaged, Healed, Died
  health->remove_listener(listener);
  health->reset();
  ARCANE_ASSERT_TRUE(!health->dead() && health->current() == 10.0f);
  const std::size_t count = events.size();
  health->apply_damage(1.0f, nullptr);
  ARCANE_ASSERT_TRUE(events.size() == count);
}

void test_destroy_on_death() {
  auto registry = make_registry();
  Scene scene;
  Node* node = scene.create_node("Doomed");
  auto* health = add<Health>(scene, node, "Health", registry);
  health->destroy_on_death = true;
  scene.update(kDt);
  const NodeId id = node->id();
  health->kill();
  ARCANE_ASSERT_TRUE(health->dead());
  scene.update(kDt);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(scene.find_node(id) == nullptr);

  Node* stays = scene.create_node("Stays");
  auto* h2 = add<Health>(scene, stays, "Health", registry);
  scene.update(kDt);
  h2->kill();
  scene.update(kDt);
  scene.update(kDt);
  ARCANE_ASSERT_TRUE(scene.find_node_by_name("Stays") && h2->dead());
}

void test_projectile_damages_and_despawns() {
  auto registry = make_registry();
  Scene scene;
  Target enemy = add_enemy(scene, registry, {100.0f, 0.0f});
  enemy.hurtbox->damage_multiplier = 2.0f;
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, 1 << kEnemyLayer);
  const NodeId shot_id = shot.node->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 60);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 10.0f - 3.0f * 2.0f);
  ARCANE_ASSERT_TRUE(enemy.health->last_event()->source == shot_id);
  run(scene, world, 2);
  ARCANE_ASSERT_TRUE(scene.find_node(shot_id) == nullptr);
}

void test_layer_mask_filters_targets() {
  auto registry = make_registry();
  Scene scene;
  Target enemy = add_enemy(scene, registry, {100.0f, 0.0f});
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, 1 << kWallLayer);  // does not react to the enemy layer
  const NodeId shot_id = shot.node->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 40);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 10.0f);
  ARCANE_ASSERT_TRUE(scene.find_node(shot_id) != nullptr);  // flew through, still alive
}

void test_wall_despawns_projectile() {
  auto registry = make_registry();
  Scene scene;
  Node* wall = scene.create_node("Wall");
  wall->local_transform().position = {100.0f, 0.0f};
  add<RigidBody2D>(scene, wall, "RigidBody2D", registry)->body_type = static_cast<int>(BodyType::Static);
  add<Collider2D>(scene, wall, "Collider2D", registry)->layer = kWallLayer;
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, kAllCollisionLayers);
  const NodeId shot_id = shot.node->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 40);
  ARCANE_ASSERT_TRUE(scene.find_node(shot_id) == nullptr);
  ARCANE_ASSERT_TRUE(scene.find_node(wall->id()) != nullptr);

  // stop_on_solid off: the shot keeps flying through (it is a sensor).
  Shot passing = add_shot(scene, registry, {0.0f, 0.0f}, kAllCollisionLayers);
  passing.projectile->stop_on_solid = false;
  const NodeId passing_id = passing.node->id();
  world.rebuild(scene);
  run(scene, world, 40);
  ARCANE_ASSERT_TRUE(scene.find_node(passing_id) != nullptr);
}

void test_lifetime_expiry() {
  auto registry = make_registry();
  Scene scene;
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, kAllCollisionLayers);
  shot.projectile->lifetime = 0.5f;
  const NodeId id = shot.node->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 20);
  ARCANE_ASSERT_TRUE(scene.find_node(id) != nullptr);
  ARCANE_ASSERT_TRUE(shot.node->local_transform().position.x > 50.0f);
  run(scene, world, 20);
  ARCANE_ASSERT_TRUE(scene.find_node(id) == nullptr);
}

void test_pierce() {
  auto registry = make_registry();
  Scene scene;
  Target first = add_enemy(scene, registry, {80.0f, 0.0f});
  Target second = add_enemy(scene, registry, {160.0f, 0.0f});
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, 1 << kEnemyLayer);
  shot.projectile->pierce = 1;
  shot.projectile->lifetime = 5.0f;
  const NodeId id = shot.node->id();
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 22);  // ~110 px: passed the first (80), not yet at the second (160)
  ARCANE_ASSERT_TRUE(first.health->current() == 7.0f && second.health->current() == 10.0f);
  ARCANE_ASSERT_TRUE(scene.find_node(id) != nullptr);
  run(scene, world, 40);
  ARCANE_ASSERT_TRUE(second.health->current() == 7.0f);
  ARCANE_ASSERT_TRUE(scene.find_node(id) == nullptr);
  ARCANE_ASSERT_TRUE(first.health->current() == 7.0f);
}

void test_melee_rehit_interval() {
  auto registry = make_registry();
  Scene scene;
  Target enemy = add_enemy(scene, registry, {0.0f, 0.0f}, 100.0f);
  Node* arm = scene.create_node("Sword");
  add<RigidBody2D>(scene, arm, "RigidBody2D", registry)->body_type = static_cast<int>(BodyType::Static);
  auto* collider = add<Collider2D>(scene, arm, "Collider2D", registry);
  collider->is_sensor = true;
  collider->layer = kPlayerAttackLayer;
  auto* hitbox = add<Hitbox>(scene, arm, "Hitbox", registry);
  hitbox->damage = 1.0f;
  hitbox->rehit_interval = 0.5f;
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 3);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 99.0f);
  run(scene, world, 20);  // ~0.38 s total: no second hit yet
  ARCANE_ASSERT_TRUE(enemy.health->current() == 99.0f);
  run(scene, world, 12);  // ~0.58 s: re-hit once
  ARCANE_ASSERT_TRUE(enemy.health->current() == 98.0f);
  hitbox->active = false;
  run(scene, world, 60);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 98.0f);

}

// rehit_interval 0: one hit per overlap, however long it lasts.
void test_melee_single_hit_per_overlap() {
  auto registry = make_registry();
  Scene scene;
  Target enemy = add_enemy(scene, registry, {0.0f, 0.0f}, 100.0f);
  Node* arm = scene.create_node("Sword");
  add<RigidBody2D>(scene, arm, "RigidBody2D", registry)->body_type = static_cast<int>(BodyType::Static);
  add<Collider2D>(scene, arm, "Collider2D", registry)->is_sensor = true;
  auto* hitbox = add<Hitbox>(scene, arm, "Hitbox", registry);
  hitbox->rehit_interval = 0.0f;
  PhysicsWorld world;
  world.rebuild(scene);
  run(scene, world, 120);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 99.0f && hitbox->hits_dealt() == 1);
}

std::filesystem::path make_project(const ComponentRegistry& registry) {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_combat_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "prefabs");
  Scene scene;
  Shot shot = add_shot(scene, registry, {0.0f, 0.0f}, 1 << kEnemyLayer);
  auto prefab = Prefab::from_node(*shot.node, registry);
  ARCANE_ASSERT_TRUE(prefab.has_value());
  auto text = prefab->to_json(registry);
  ARCANE_ASSERT_TRUE(text.has_value());
  std::ofstream(dir / kAsset, std::ios::binary) << *text;
  return dir;
}

void test_pooled_projectile_release_and_reuse() {
  auto registry = make_registry();
  const auto dir = make_project(registry);
  GameContext context(dir, registry);
  set_active_game_context(&context);
  Scene scene;
  Node* spawner_node = scene.create_node("Spawner");
  auto* spawner = add<Spawner2D>(scene, spawner_node, "Spawner2D", registry);
  spawner->prefab = kAsset;
  spawner->auto_start = false;
  Target enemy = add_enemy(scene, registry, {100.0f, 0.0f}, 100.0f);
  PhysicsWorld world;
  world.rebuild(scene);
  scene.update(kDt);

  Node* shot = spawner->spawn({0.0f, 0.0f});
  ARCANE_ASSERT_TRUE(shot != nullptr);
  const NodeId id = shot->id();
  auto* projectile = static_cast<Projectile*>(shot->find_component("Projectile"));
  projectile->launch({1.0f, 0.0f}, 300.0f);
  world.rebuild(scene);  // the spawned body joins the world
  ARCANE_ASSERT_TRUE(spawner->alive_count() == 1 && spawner->free_count() == 0);
  run(scene, world, 60);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 97.0f);
  ARCANE_ASSERT_TRUE(spawner->alive_count() == 0 && spawner->free_count() == 1);
  ARCANE_ASSERT_TRUE(!shot->active() && scene.find_node(id) == shot);

  Node* again = spawner->spawn({0.0f, 0.0f});
  ARCANE_ASSERT_TRUE(again == shot && again->active());
  projectile = static_cast<Projectile*>(again->find_component("Projectile"));
  ARCANE_ASSERT_TRUE(projectile->hurtbox_hits() == 0);
  projectile->launch({1.0f, 0.0f}, 300.0f);
  run(scene, world, 60);
  ARCANE_ASSERT_TRUE(enemy.health->current() == 94.0f);  // hit again after reuse
  ARCANE_ASSERT_TRUE(spawner->free_count() == 1);

  // A non-pooled enemy dying with destroy_on_death off stays dead (release_on_death finds no pool).
  enemy.health->kill();
  ARCANE_ASSERT_TRUE(enemy.health->dead() && scene.find_node(enemy.node->id()));
  set_active_game_context(nullptr);
}

}  // namespace

int main() {
  test_health();
  test_destroy_on_death();
  test_projectile_damages_and_despawns();
  test_layer_mask_filters_targets();
  test_wall_despawns_projectile();
  test_lifetime_expiry();
  test_pierce();
  test_melee_rehit_interval();
  test_melee_single_hit_per_overlap();
  test_pooled_projectile_release_and_reuse();
  return 0;
}
