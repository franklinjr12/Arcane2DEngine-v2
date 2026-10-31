#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "arcane/combat.hpp"
#include "arcane/game_context.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/navigation.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
#include "arcane/spawner.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "test_assert.hpp"

namespace {
arcane::Node* find(arcane::Node& root, const char* name) {
  if (root.name() == name) return &root;
  for (const auto& child : root.children()) if (auto* n = find(*child, name)) return n;
  return nullptr;
}
void text_roundtrip() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_TOPDOWN_PROJECT_DIR, ARCANE_TOPDOWN_MODULE_PATH);
  ARCANE_ASSERT_TRUE(runtime.has_value());
  std::ifstream file(std::string(ARCANE_TOPDOWN_PROJECT_DIR) + "/scenes/main.scene.json");
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  file.close();
  const auto pos = text.find("\"enemy_speed\": 65.0");
  ARCANE_ASSERT_TRUE(pos != std::string::npos);
  text.replace(pos, std::string("\"enemy_speed\": 65.0").size(), "\"enemy_speed\": 90.0");
  auto scene = arcane::Scene::load_from_json_strict(text, runtime->registry());
  ARCANE_ASSERT_TRUE(scene.has_value());
  auto saved = scene->save_canonical_json(runtime->registry());
  ARCANE_ASSERT_TRUE(saved.has_value());
  auto reopened = arcane::Scene::load_from_json_strict(*saved, runtime->registry());
  ARCANE_ASSERT_TRUE(reopened.has_value());
  auto saved_again = reopened->save_canonical_json(runtime->registry());
  ARCANE_ASSERT_TRUE(saved_again.has_value() && *saved_again == *saved);
  ARCANE_ASSERT_TRUE(saved->find("\"enemy_speed\": 90") != std::string::npos);
  text.replace(pos, std::string("\"enemy_speed\"").size(), "\"unknown_speed\"");
  ARCANE_ASSERT_TRUE(!arcane::Scene::load_from_json_strict(text, runtime->registry()).has_value());
}
// Active pooled nodes called `name` (the spawners park their unused instances inactive under the same name).
void active_named(arcane::Node& node, const char* name, std::vector<arcane::Node*>& out) {
  if (node.name() == name && node.active()) out.push_back(&node);
  for (const auto& child : node.children()) active_named(*child, name, out);
}
std::vector<arcane::Node*> live(arcane::Scene& scene, const char* name) {
  std::vector<arcane::Node*> out;
  for (const auto& root : scene.roots()) active_named(*root, name, out);
  return out;
}
void set_property(arcane::editor::ProjectRuntime& runtime, arcane::Component& component, const char* type,
                  const std::string& name, arcane::PropertyValue value) {
  for (const auto& property : *runtime.registry().properties_of(type))
    if (property.name == name) { property.set(component, value); return; }
  ARCANE_ASSERT_TRUE(false);
}
int coins_in(arcane::Hud& hud) {
  const auto* text = hud.find_text("score_text");
  ARCANE_ASSERT_TRUE(text && text->text.rfind("COINS ", 0) == 0);
  return std::stoi(text->text.substr(6));
}
void simulate() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_TOPDOWN_PROJECT_DIR, ARCANE_TOPDOWN_MODULE_PATH);
  if (!runtime) std::fprintf(stderr, "%s\n", runtime.error().c_str());
  ARCANE_ASSERT_TRUE(runtime.has_value());
  auto& scene = runtime->session().scene();
  auto& root = *scene.roots().front();
  auto* player = find(root, "Player"); auto* burst = find(root, "HitBurst");
  auto* enemy_spawner_node = find(root, "EnemySpawner"); auto* coin_spawner_node = find(root, "CoinSpawner");
  auto* rules = root.find_component("ArenaRules");
  ARCANE_ASSERT_TRUE(player && burst && enemy_spawner_node && coin_spawner_node && rules);
  ARCANE_ASSERT_TRUE(live(scene, "Enemy").empty());  // no authored enemy: waves spawn them
  auto* emitter = static_cast<arcane::ParticleEmitter*>(burst->find_component("ParticleEmitter"));
  auto* player_health = static_cast<arcane::Health*>(player->find_component("Health"));
  auto* enemy_spawner = static_cast<arcane::Spawner2D*>(enemy_spawner_node->find_component("Spawner2D"));
  auto* coin_spawner = static_cast<arcane::Spawner2D*>(coin_spawner_node->find_component("Spawner2D"));
  ARCANE_ASSERT_TRUE(emitter && player_health && enemy_spawner && coin_spawner && player->find_component("TopDownController2D"));
  ARCANE_ASSERT_TRUE(enemy_spawner->prefab == "assets/enemy.prefab.json" && coin_spawner->prefab == "assets/coin.prefab.json");
  // Short waves keep the test fast; the player survives the contact damage so only the waves are under test.
  set_property(*runtime, *rules, "ArenaRules", "wave_duration", 8.0f);
  set_property(*runtime, *rules, "ArenaRules", "intermission", 1.0f);
  player_health->max_health = 1000.0f; player_health->reset();
  arcane::GameContext context(ARCANE_TOPDOWN_PROJECT_DIR, runtime->registry());
  arcane::set_active_game_context(&context);
  arcane::Hud hud = runtime->session().project().hud;
  arcane::InputActionMap input = runtime->session().project().input_actions;
  arcane::FakeInputBackend keys;
  arcane::set_active_hud(&hud); arcane::set_active_input_map(&input);
  arcane::PhysicsWorld physics; physics.rebuild(scene);
  using arcane::InputDevice;
  arcane::Node* target = nullptr;  // first enemy of wave 1, staged on the player's row so bullets can hit it
  arcane::NodeId target_id = 0;
  bool killed = false, detour = false, particles = false, collected = false, wave2 = false;
  int stage = -1, volley = -1, enemy_speed_ok = 0;
  for (int i = 0; i < 12 * 60; ++i) {
    int hold = 0, fire = 0;
    const auto pp = player->world_transform().position;
    if (!target) {
      auto enemies = live(scene, "Enemy");
      if (i == 5 && !enemies.empty()) {  // wave 1 spawned its first enemy at the start, away from the player
        ARCANE_ASSERT_TRUE(enemy_spawner->alive_count() == 1 && enemy_spawner->free_count() >= 7 && !enemy_spawner->running());  // prewarmed, manual
        target = enemies.front(); target_id = target->id();
        const auto sp = target->world_transform().position;
        ARCANE_ASSERT_TRUE(std::hypot(sp.x - pp.x, sp.y - pp.y) >= 160.0f);
        static_cast<arcane::RigidBody2D*>(target->find_component("RigidBody2D"))->teleport({pp.x + 100.0f, pp.y});
        static_cast<arcane::Health*>(target->find_component("Health"))->add_listener(
            [&](const arcane::HealthEvent& e) { killed |= e.kind == arcane::HealthEvent::Kind::Died; });
        enemy_speed_ok = static_cast<arcane::SteeringAgent2D*>(target->find_component("SteeringAgent2D"))->max_speed == 65.0f;
        stage = i; volley = i + 2;
      }
    } else if (!killed && i >= stage + 1 && i < stage + 400) {
      if (i == volley - 1) hold = arcane::Keys::kD;  // face the enemy standing to the right
      if (i == volley) { fire = arcane::Keys::kSpace; volley += 33; }
    } else if (killed && !collected) {
      // Walk onto the coin the dead enemy dropped.
      auto coins = live(scene, "Coin");
      if (!coins.empty()) {
        const auto cp = coins.front()->world_transform().position;
        const arcane::Vec2 d{cp.x - pp.x, cp.y - pp.y};
        hold = std::abs(d.x) >= std::abs(d.y) ? (d.x > 0 ? arcane::Keys::kD : arcane::Keys::kA)
                                               : (d.y > 0 ? arcane::Keys::kS : arcane::Keys::kW);
      }
      collected = coins_in(hud) >= 1;
    }
    for (int key : {arcane::Keys::kA, arcane::Keys::kD, arcane::Keys::kS, arcane::Keys::kW})
      keys.set_down(InputDevice::Keyboard, key, key == hold);
    keys.set_down(InputDevice::Keyboard, arcane::Keys::kSpace, fire != 0);
    input.update(keys); runtime->module_api()->on_update(1.0f/60.0f);
    scene.update(1.0f/60.0f); physics.step(scene, 1.0f/60.0f);
    if (i < 60 * 6)  // an enemy that must go around the interior wall gets a multi-waypoint A* path
      for (auto* enemy : live(scene, "Enemy"))
        detour |= static_cast<arcane::SteeringAgent2D*>(enemy->find_component("SteeringAgent2D"))->path().size() > 1;
    if (i == 60 * 6) {  // inside wave 1: more of its enemies came from the spawner
      ARCANE_ASSERT_TRUE(enemy_spawner->alive_count() >= 2);
      ARCANE_ASSERT_TRUE(hud.find_text("wave_text")->text.rfind("WAVE 1", 0) == 0);
    }
    if (i == 60 * 8 + 30) {  // intermission: the wave timer released what was left
      ARCANE_ASSERT_TRUE(live(scene, "Enemy").empty() && enemy_spawner->alive_count() == 0);
      ARCANE_ASSERT_TRUE(hud.find_text("wave_text")->text.rfind("BREAK", 0) == 0);
    }
    particles |= !emitter->particles().empty();
    wave2 |= hud.find_text("wave_text")->text.rfind("WAVE 2", 0) == 0;
  }
  ARCANE_ASSERT_TRUE(target && enemy_speed_ok);
  ARCANE_ASSERT_TRUE(detour);
  ARCANE_ASSERT_TRUE(killed);  // the pooled bullets killed the enemy: Health Died fired
  ARCANE_ASSERT_TRUE(collected && coins_in(hud) == 1);  // coin dropped at its position, picked up, HUD updated
  ARCANE_ASSERT_TRUE(coin_spawner->alive_count() == 0 && coin_spawner->free_count() == 8);  // coin went back to its pool
  ARCANE_ASSERT_TRUE(wave2 && enemy_spawner->alive_count() >= 1 && !live(scene, "Enemy").empty());  // wave 2 started
  ARCANE_ASSERT_TRUE(player_health->current() < 1000.0f && particles);  // enemies reached the player and hurt it
  arcane::set_active_input_map(nullptr); arcane::set_active_hud(nullptr); arcane::set_active_game_context(nullptr);
}
}
int main() { text_roundtrip(); simulate(); }
