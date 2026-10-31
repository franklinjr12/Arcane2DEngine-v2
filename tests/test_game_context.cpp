// GP1: gameplay reaching its scene (spawn / deferred destroy), teleport, runtime asset lookup, audio by asset id
// and scene restart. Headless: null audio backend, no window.
#include <cmath>
#include <cstdlib>
#include <limits>
#include <filesystem>
#include <fstream>
#include <memory>

#include "arcane/assets.hpp"
#include "arcane/audio.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/input.hpp"
#include "arcane/physics.hpp"
#include "arcane/replay.hpp"
#include "arcane/save_game.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;

// Counts ticks; at tick `act_at` it destroys its node ("destroy"), spawns a body ("spawn") or asks for a restart.
class Script : public arcane::Component {
 public:
  std::string action;
  int act_at = 3;
  void on_update(float) override {
    if (++ticks_ != act_at) return;
    if (action == "destroy") {
      scene()->destroy_node(node()->id());
    } else if (action == "spawn") {
      arcane::Node* fresh = scene()->create_node("Spawned");
      fresh->local_transform().position = {50.0f, 0.0f};
      scene()->add_component(fresh, "RigidBody2D", arcane::active_game_context()->registry());
    } else if (action == "restart") {
      scene()->destroy_node(scene()->find_node_by_name("Victim")->id());
      arcane::active_game_context()->request_restart();
    }
  }

 private:
  int ticks_ = 0;
};

// Counts on_update calls and moves right by 60 units/s * dt, so a paused scene shows calls without movement.
class Mover : public arcane::Component {
 public:
  static inline int calls = 0;
  void on_update(float dt) override {
    ++calls;
    node()->local_transform().position.x += 60.0f * dt;
  }
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);
  registry.register_component("Mover", [] { return std::make_unique<Mover>(); }, {});
  registry.register_component("Script", [] { return std::make_unique<Script>(); }, {});
  return registry;
}

arcane::Node* add_body(arcane::Scene& scene, const arcane::ComponentRegistry& registry, const char* name) {
  arcane::Node* node = scene.create_node(name);
  scene.add_component(node, "RigidBody2D", registry);
  scene.add_component(node, "Collider2D", registry);
  return node;
}

void test_teleport_moves_body_and_stops_it() {
  auto registry = make_registry();
  arcane::Scene scene;
  arcane::Node* box = add_body(scene, registry, "Box");
  auto* body = static_cast<arcane::RigidBody2D*>(box->find_component("RigidBody2D"));
  arcane::PhysicsWorld physics;
  physics.rebuild(scene);
  for (int i = 0; i < 30; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
  }
  ARCANE_ASSERT_TRUE(body->linear_velocity.y > 50.0f);  // falling (y down)
  body->teleport({500.0f, -200.0f});
  ARCANE_ASSERT_TRUE(box->local_transform().position.x == 500.0f);  // node moves at once
  scene.update(kDt);
  physics.step(scene, kDt);
  ARCANE_ASSERT_TRUE(std::abs(box->local_transform().position.x - 500.0f) < 0.01f);
  ARCANE_ASSERT_TRUE(std::abs(box->local_transform().position.y + 200.0f) < 2.0f);  // one step of gravity at most
  ARCANE_ASSERT_TRUE(body->linear_velocity.y < 20.0f);  // the old fall speed is gone
}

void test_viewport_size() {
  auto registry = make_registry();
  arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
  ARCANE_ASSERT_TRUE(context.viewport_size().width == 800 && context.viewport_size().height == 600);
  context.set_viewport_size(1280, 720);
  ARCANE_ASSERT_TRUE(context.viewport_size().width == 1280 && context.viewport_size().height == 720);
}

void test_destroy_and_spawn_during_update_keep_bodies_in_sync() {
  auto registry = make_registry();
  arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
  arcane::set_active_game_context(&context);
  arcane::Scene scene;
  arcane::Node* doomed = add_body(scene, registry, "Doomed");
  scene.add_component(doomed, "Script", registry);
  static_cast<Script*>(doomed->find_component("Script"))->action = "destroy";
  arcane::Node* spawner = scene.create_node("Spawner");
  scene.add_component(spawner, "Script", registry);
  static_cast<Script*>(spawner->find_component("Script"))->action = "spawn";
  arcane::PhysicsWorld physics;
  physics.rebuild(scene);
  ARCANE_ASSERT_TRUE(physics.body_count() == 1);
  for (int i = 0; i < 6; ++i) {
    scene.update(kDt);
    physics.step(scene, kDt);
  }
  ARCANE_ASSERT_TRUE(scene.find_node_by_name("Doomed") == nullptr);
  ARCANE_ASSERT_TRUE(scene.find_node_by_name("Spawned") != nullptr);
  ARCANE_ASSERT_TRUE(physics.body_count() == 1);  // Doomed's body gone, Spawned's body created
  arcane::set_active_game_context(nullptr);
}

void test_scene_pointer_survives_moves() {
  auto registry = make_registry();
  arcane::Scene a;
  arcane::Node* node = a.create_node("N");
  ARCANE_ASSERT_TRUE(node->scene() == &a);
  arcane::Scene b = std::move(a);
  ARCANE_ASSERT_TRUE(node->scene() == &b);
  arcane::Scene c;
  c = std::move(b);
  ARCANE_ASSERT_TRUE(node->scene() == &c);
  ARCANE_ASSERT_TRUE(c.find_node_by_name("N") == node);
}

void test_asset_lookup_and_audio_by_id() {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_gp1_project";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "assets" / "sfx");
  { std::ofstream(dir / "assets" / "sfx" / "coin.wav") << "x"; }
  auto registry = make_registry();
  arcane::register_audio_components(registry);
  arcane::GameContext context(dir, registry);
  auto audio = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(audio.has_value());

  const arcane::AssetId coin = arcane::asset_id_for_relative_path("sfx/coin.wav");
  ARCANE_ASSERT_TRUE(context.asset_path(coin) == dir / "assets" / "sfx" / "coin.wav");
  ARCANE_ASSERT_TRUE(!context.asset_path(arcane::asset_id_for_relative_path("nope.wav")));

  ARCANE_ASSERT_TRUE(!context.play_sound(coin));  // no audio system installed yet
  arcane::set_active_audio_system(&*audio);
  arcane::set_active_game_context(&context);
  ARCANE_ASSERT_TRUE(context.play_sound(coin, 0.5f));
  ARCANE_ASSERT_TRUE(context.play_sound(std::string("sfx/coin.wav")));
  ARCANE_ASSERT_TRUE(!context.play_sound(std::string("sfx/missing.wav")));
  ARCANE_ASSERT_TRUE(arcane::play_one_shot(coin));
  arcane::AudioSource source;
  source.clip = coin;
  source.loop = true;
  ARCANE_ASSERT_TRUE(source.play());
  ARCANE_ASSERT_TRUE(audio->commands().size() == 4);
  ARCANE_ASSERT_TRUE(audio->commands()[0].file == dir / "assets" / "sfx" / "coin.wav");  // absolute: cwd-independent
  ARCANE_ASSERT_TRUE(audio->commands()[0].volume == 0.5f);
  ARCANE_ASSERT_TRUE(audio->commands()[3].loop && audio->commands()[3].bus == arcane::AudioBus::Music);
  arcane::set_active_game_context(nullptr);
  arcane::set_active_audio_system(nullptr);
  ARCANE_ASSERT_TRUE(!source.play());  // no context: nothing happens
  std::filesystem::remove_all(dir);
}

void test_restart_reloads_authored_scene() {
  auto registry = make_registry();
  arcane::Scene authored;
  authored.create_node("Victim");
  arcane::Node* driver = authored.create_node("Driver");
  authored.add_component(driver, "Script", registry);
  static_cast<Script*>(driver->find_component("Script"))->action = "restart";
  auto json = authored.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(json.has_value());

  auto scene = arcane::Scene::load_from_json(*json, registry);
  ARCANE_ASSERT_TRUE(scene.has_value());
  arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
  arcane::set_active_game_context(&context);
  arcane::InputActionMap actions;
  arcane::FixedRunOptions options;
  options.frames = 3;  // Script acts on tick 3: destroys Victim and requests the restart
  options.restart_scene_json = *json;
  auto hashes = arcane::run_fixed_frames(*scene, registry, actions, options);
  ARCANE_ASSERT_TRUE(hashes.has_value());
  ARCANE_ASSERT_TRUE(scene->find_node_by_name("Victim") != nullptr);  // back after the reload
  ARCANE_ASSERT_TRUE(scene->find_node_by_name("Driver")->scene() == &*scene);
  arcane::set_active_game_context(nullptr);
}

void test_time_scale_clamps() {
  auto registry = make_registry();
  arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
  ARCANE_ASSERT_TRUE(context.time_scale() == 1.0f && !context.paused());
  context.set_time_scale(-3.0f);
  ARCANE_ASSERT_TRUE(context.time_scale() == 0.0f && context.paused());
  context.set_time_scale(100.0f);
  ARCANE_ASSERT_TRUE(context.time_scale() == 8.0f);
  context.set_time_scale(std::numeric_limits<float>::quiet_NaN());
  ARCANE_ASSERT_TRUE(context.time_scale() == 1.0f);
  context.set_time_scale(std::numeric_limits<float>::infinity());
  ARCANE_ASSERT_TRUE(context.time_scale() == 1.0f);
  context.set_unscaled_delta(0.5f);
  ARCANE_ASSERT_TRUE(context.unscaled_delta() == 0.5f);
}

void test_time_scale_in_fixed_runs() {
  auto registry = make_registry();
  arcane::Scene authored;
  arcane::Node* node = authored.create_node("M");
  authored.add_component(node, "Mover", registry);
  auto json = authored.save_canonical_json(registry);
  ARCANE_ASSERT_TRUE(json.has_value());
  arcane::InputActionMap actions;
  arcane::FixedRunOptions options;
  options.frames = 20;

  auto run = [&](float scale, int& calls, float& x) {
    auto scene = arcane::Scene::load_from_json(*json, registry);
    ARCANE_ASSERT_TRUE(scene.has_value());
    arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
    context.set_time_scale(scale);
    arcane::set_active_game_context(&context);
    Mover::calls = 0;
    auto hashes = arcane::run_fixed_frames(*scene, registry, actions, options);
    arcane::set_active_game_context(nullptr);
    ARCANE_ASSERT_TRUE(hashes.has_value());
    calls = Mover::calls;
    x = scene->find_node_by_name("M")->local_transform().position.x;
    return *hashes;
  };
  int calls_paused = 0, calls_one = 0;
  float x_paused = 1.0f, x_one = 0.0f;
  auto paused = run(0.0f, calls_paused, x_paused);
  auto scaled = run(1.0f, calls_one, x_one);

  // Baseline without any context: identical to scale 1.
  auto baseline_scene = arcane::Scene::load_from_json(*json, registry);
  auto baseline = arcane::run_fixed_frames(*baseline_scene, registry, actions, options);
  ARCANE_ASSERT_TRUE(baseline.has_value());
  ARCANE_ASSERT_TRUE(scaled == *baseline);
  ARCANE_ASSERT_TRUE(x_one > 0.0f);
  ARCANE_ASSERT_TRUE(calls_paused == 20 && calls_one == 20);  // components keep updating while paused
  ARCANE_ASSERT_TRUE(x_paused == 0.0f);                       // ... but at dt 0
  ARCANE_ASSERT_TRUE(paused.front() == paused.back());        // state never changes
}

void test_save_helpers() {
  auto registry = make_registry();
  const auto dir = std::filesystem::temp_directory_path() / "arcane_gp6_saves";
  std::filesystem::remove_all(dir);
  arcane::GameContext context(std::filesystem::temp_directory_path(), registry);
  ARCANE_ASSERT_TRUE(!context.saves_enabled());
  ARCANE_ASSERT_TRUE(!context.load_save("slot1"));
  ARCANE_ASSERT_TRUE(!context.write_save("slot1", arcane::SaveData{}));

  ::setenv("ARCANE_SAVE_DIR", dir.string().c_str(), 1);
  arcane::SaveConfig config;
  config.game_id = "demo";
  config.data_version = 3;
  context.set_save_config(config, true);
  ARCANE_ASSERT_TRUE(context.saves_enabled());
  auto missing = context.load_save("slot1");
  ARCANE_ASSERT_TRUE(missing.has_value() && missing->values.empty() && missing->data_version == 3);
  arcane::SaveData data;
  data.data_version = 3;
  data.set("best_score", 120);
  ARCANE_ASSERT_TRUE(context.write_save("slot1", data).has_value());
  auto loaded = context.load_save("slot1");
  ARCANE_ASSERT_TRUE(loaded.has_value() && loaded->get_int("best_score") == 120);
  ARCANE_ASSERT_TRUE(!context.load_save("bad slot"));
  config.game_id = "";
  context.set_save_config(config, true);
  ARCANE_ASSERT_TRUE(!context.load_save("slot1"));  // no game_id
  ::unsetenv("ARCANE_SAVE_DIR");
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  test_time_scale_clamps();
  test_time_scale_in_fixed_runs();
  test_save_helpers();
  test_teleport_moves_body_and_stops_it();
  test_viewport_size();
  test_destroy_and_spawn_during_update_keep_bodies_in_sync();
  test_scene_pointer_survives_moves();
  test_asset_lookup_and_audio_by_id();
  test_restart_reloads_authored_scene();
  return 0;
}
