#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
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
  ARCANE_ASSERT_TRUE(emitter);
  arcane::Hud hud = runtime->session().project().hud;
  arcane::InputActionMap input = runtime->session().project().input_actions;
  arcane::FakeInputBackend keys;
  arcane::set_active_hud(&hud); arcane::set_active_input_map(&input);
  arcane::PhysicsWorld physics; physics.rebuild(scene);
  const float initial_enemy_x = enemy->world_transform().position.x;
  bool particles = false;
  for (int i=0; i<600; ++i) {
    keys.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kA, i < 180);
    input.update(keys); runtime->module_api()->on_update(1.0f/60.0f);
    scene.update(1.0f/60.0f); physics.step(scene, 1.0f/60.0f);
    if (i == 179) {
      const auto pos = player->world_transform().position;
      ARCANE_ASSERT_TRUE(pos.x >= 40.0f && pos.x < 60.0f); // solid left wall
      ARCANE_ASSERT_TRUE(std::abs(pos.y - 240.0f) < 1.0f); // no gravity
      ARCANE_ASSERT_TRUE(enemy->world_transform().position.x < initial_enemy_x - 50.0f);
    }
    particles |= !emitter->particles().empty();
  }
  bool damaged = false, bar_changed = false;
  for (const auto& e : hud.elements()) {
    if (const auto* t = std::get_if<arcane::HudText>(&e)) damaged |= t->text != "HEALTH 5";
    if (const auto* b = std::get_if<arcane::HudProgressBar>(&e)) bar_changed |= b->fraction < 1.0f;
  }
  ARCANE_ASSERT_TRUE(damaged && bar_changed && particles);
  arcane::set_active_input_map(nullptr); arcane::set_active_hud(nullptr);
}
}
int main() { text_roundtrip(); simulate(); }
