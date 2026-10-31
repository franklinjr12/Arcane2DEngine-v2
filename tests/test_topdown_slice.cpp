#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
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
void simulate() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_TOPDOWN_PROJECT_DIR, ARCANE_TOPDOWN_MODULE_PATH);
  if (!runtime) std::fprintf(stderr, "%s\n", runtime.error().c_str());
  ARCANE_ASSERT_TRUE(runtime.has_value());
  auto& scene = runtime->session().scene();
  auto& root = *scene.roots().front();
  auto* player = find(root, "Player"); auto* enemy = find(root, "Enemy"); auto* burst = find(root, "HitBurst");
  ARCANE_ASSERT_TRUE(player && enemy && burst && root.find_component("ArenaRules"));
  auto* emitter = static_cast<arcane::ParticleEmitter*>(burst->find_component("ParticleEmitter"));
  auto* player_health = static_cast<arcane::Health*>(player->find_component("Health"));
  auto* enemy_health = static_cast<arcane::Health*>(enemy->find_component("Health"));
  auto* agent = static_cast<arcane::SteeringAgent2D*>(enemy->find_component("SteeringAgent2D"));
  ARCANE_ASSERT_TRUE(emitter && player_health && enemy_health && agent && player->find_component("TopDownController2D"));
  ARCANE_ASSERT_TRUE(agent->max_speed == 65.0f);  // ArenaRules::enemy_speed
  const arcane::NodeId enemy_id = enemy->id();
  int enemy_hits = 0;
  enemy_health->add_listener([&](const arcane::HealthEvent& e) { enemy_hits += e.kind != arcane::HealthEvent::Kind::Healed; });
  arcane::GameContext context(ARCANE_TOPDOWN_PROJECT_DIR, runtime->registry());
  arcane::set_active_game_context(&context);
  arcane::Hud hud = runtime->session().project().hud;
  arcane::InputActionMap input = runtime->session().project().input_actions;
  arcane::FakeInputBackend keys;
  arcane::set_active_hud(&hud); arcane::set_active_input_map(&input);
  arcane::PhysicsWorld physics; physics.rebuild(scene);
  const float initial_enemy_x = enemy->world_transform().position.x;
  bool particles = false, detour = false, shot = false;
  int next_volley = -1, volley = 0;
  using arcane::InputDevice;
  for (int i=0; i<1800; ++i) {
    // Hold A into the left wall first; then, once the enemy has hurt the player, face it and tap Space every 33 frames.
    int hold = i < 180 ? arcane::Keys::kA : 0, fire = 0;
    auto* live_enemy = scene.find_node(enemy_id);
    if (i >= 180 && live_enemy && player_health->current() < 5.0f) {
      if (next_volley < 0) next_volley = i;
      if (i == next_volley) {
        const auto ep = live_enemy->world_transform().position, pp = player->world_transform().position;
        const arcane::Vec2 d{ep.x - pp.x, ep.y - pp.y};
        hold = std::abs(d.x) >= std::abs(d.y) ? (d.x > 0 ? arcane::Keys::kD : arcane::Keys::kA)
                                               : (d.y > 0 ? arcane::Keys::kS : arcane::Keys::kW);
        volley = i + 1;
        next_volley += 33;
      }
    }
    if (i == volley && i > 0) fire = arcane::Keys::kSpace;
    for (int key : {arcane::Keys::kA, arcane::Keys::kD, arcane::Keys::kS, arcane::Keys::kW})
      keys.set_down(InputDevice::Keyboard, key, key == hold);
    keys.set_down(InputDevice::Keyboard, arcane::Keys::kSpace, fire != 0);
    input.update(keys); runtime->module_api()->on_update(1.0f/60.0f);
    scene.update(1.0f/60.0f); physics.step(scene, 1.0f/60.0f);
    if (i == 30) detour = agent->path().size() > 1;  // the interior wall is between them: A* routes around it
    if (i == 179) {
      const auto pos = player->world_transform().position;
      ARCANE_ASSERT_TRUE(pos.x >= 40.0f && pos.x < 60.0f); // solid left wall
      ARCANE_ASSERT_TRUE(std::abs(pos.y - 240.0f) < 1.0f); // no gravity
      ARCANE_ASSERT_TRUE(enemy->world_transform().position.x < initial_enemy_x - 50.0f);
    }
    shot |= fire != 0;
    particles |= !emitter->particles().empty();
    if (!scene.find_node(enemy_id) && i > 400) break;
  }
  ARCANE_ASSERT_TRUE(detour);
  ARCANE_ASSERT_TRUE(player_health->current() < 5.0f);  // the enemy got around the wall and hurt the player
  ARCANE_ASSERT_TRUE(shot && enemy_hits >= 1);          // a bullet from the pooled Spawner2D hit the enemy's Hurtbox
  ARCANE_ASSERT_TRUE(enemy_health->current() < enemy_health->max_health || !scene.find_node(enemy_id));
  bool damaged = false, bar_changed = false;
  for (const auto& e : hud.elements()) {
    if (const auto* t = std::get_if<arcane::HudText>(&e)) damaged |= t->text != "HEALTH 5";
    if (const auto* b = std::get_if<arcane::HudProgressBar>(&e)) bar_changed |= b->fraction < 1.0f;
  }
  ARCANE_ASSERT_TRUE(damaged && bar_changed && particles);
  arcane::set_active_input_map(nullptr); arcane::set_active_hud(nullptr); arcane::set_active_game_context(nullptr);
}
}
int main() { text_roundtrip(); simulate(); }
