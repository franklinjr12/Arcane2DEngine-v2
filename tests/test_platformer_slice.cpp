// Headless Play simulation of samples/platformer_slice: opens the project with its
// real game module, then runs N fixed steps with scripted input (no window/GL/audio
// device). Mirrors the arcane_player loop minus rendering.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/animgraph.hpp"
#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/game_context.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/project_runtime.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Outcome {
  float player_x = 0.0f, player_y = 0.0f, camera_x = 0.0f;
  std::string score_text, banner_text;
  float health = 0.0f;
  bool saw_flip = false, saw_run_clip = false, saw_particles = false;
  std::size_t coin_cues = 0, hurt_cues = 0;
  int steps_run = 0;
  float respawn_x = -1.0f;  // player x right after the first backwards jump (a pit respawn)
};

arcane::Node* find(arcane::Node* n, const std::string& name) {
  if (n->name() == name) return n;
  for (const auto& c : n->children())
    if (auto* r = find(c.get(), name)) return r;
  return nullptr;
}

// Holds right; taps jump on a fixed cadence (unless `jump` is false). `steps` fixed updates.
Outcome simulate(int steps, bool jump = true, bool spikes = true) {
  Outcome out;
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_SLICE_PROJECT_DIR, ARCANE_SLICE_MODULE_PATH);
  if (!runtime) std::fprintf(stderr, "open failed: %s\n", runtime.error().c_str());
  ARCANE_ASSERT_TRUE(runtime.has_value());
  arcane::Scene& scene = runtime->session().scene();

  arcane::Hud hud = runtime->session().project().hud;
  arcane::set_active_hud(&hud);
  auto audio = arcane::AudioSystem::create(true);
  ARCANE_ASSERT_TRUE(audio.has_value());
  arcane::set_active_audio_system(&*audio);
  arcane::GameContext context(ARCANE_SLICE_PROJECT_DIR, runtime->registry());
  arcane::set_active_game_context(&context);
  // The sensor body is built from the node position, so the spikes move away before the world is built.
  if (!spikes) find(scene.roots().front().get(), "Spikes")->local_transform().position = {-100000.0f, 0.0f};
  // These long runs predate the enemies and measure the spikes, pit and coins; enemy tests use Fixture below.
  for (const char* name : {"Enemy0", "Enemy1"}) find(scene.roots().front().get(), name)->set_active(false);
  arcane::PhysicsWorld physics;
  physics.rebuild(scene);
  arcane::InputActionMap input = runtime->session().project().input_actions;
  arcane::set_active_input_map(&input);
  arcane::FakeInputBackend keys;

  const arcane::AssetId run_clip = arcane::editor::asset_id_for_relative_path("hero_run.json");
  arcane::Node* level = scene.roots().front().get();
  arcane::Node* player = find(level, "Player");
  arcane::Node* camera = find(level, "MainCamera");
  arcane::Node* burst = find(level, "PickupBurst");
  ARCANE_ASSERT_TRUE(player && camera && burst);
  auto* anim = static_cast<arcane::AnimatedSprite*>(player->find_component("AnimatedSprite"));
  auto* emitter = static_cast<arcane::ParticleEmitter*>(burst->find_component("ParticleEmitter"));
  ARCANE_ASSERT_TRUE(anim && emitter);
  auto* animator = static_cast<arcane::Animator*>(player->find_component("Animator"));
  auto* sprite = static_cast<arcane::Sprite*>(player->find_component("Sprite"));
  ARCANE_ASSERT_TRUE(animator && sprite);

  float last_x = player->world_transform().position.x;
  for (int i = 0; i < steps; ++i) {
    keys.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kD, true);
    keys.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kSpace, jump && i % 70 >= 40 && i % 70 < 52);
    input.update(keys);
    runtime->module_api()->on_update(kDt);
    scene.update(kDt);
    physics.step(scene, kDt);
    out.saw_run_clip |= anim->clip == run_clip && animator->state() == "run";
    out.saw_flip |= !sprite->flip_x;
    out.saw_particles |= !emitter->particles().empty();
    ++out.steps_run;
    const float x = player->world_transform().position.x;
    if (out.respawn_x < 0.0f && x < last_x - 50.0f) out.respawn_x = x;
    last_x = x;
  }

  out.player_x = player->world_transform().position.x;
  out.player_y = player->world_transform().position.y;
  out.camera_x = static_cast<arcane::Camera2D*>(camera->find_component("Camera2D"))->view_center(800, 600).x;
  ARCANE_ASSERT_TRUE(hud.find_text("score") && hud.find_text("banner") && hud.find_bar("health"));
  out.score_text = hud.find_text("score")->text;
  out.banner_text = hud.find_text("banner")->text;
  out.health = hud.find_bar("health")->fraction;
  for (const auto& cmd : audio->commands()) {
    if (cmd.file.filename() == "coin.wav") ++out.coin_cues;
    if (cmd.file.filename() == "hurt.wav") ++out.hurt_cues;
  }
  arcane::set_active_input_map(nullptr);
  arcane::set_active_game_context(nullptr);
  arcane::set_active_audio_system(nullptr);
  arcane::set_active_hud(nullptr);
  return out;
}

void test_project_loads_with_authored_hud() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_SLICE_PROJECT_DIR, ARCANE_SLICE_MODULE_PATH);
  ARCANE_ASSERT_TRUE(runtime.has_value());
  const arcane::Hud& hud = runtime->session().project().hud;
  bool text = false, bar = false, button = false;
  for (const auto& e : hud.elements()) {
    text |= std::holds_alternative<arcane::HudText>(e);
    bar |= std::holds_alternative<arcane::HudProgressBar>(e);
    button |= std::holds_alternative<arcane::HudButton>(e);
  }
  ARCANE_ASSERT_TRUE(text && bar && button);
  ARCANE_ASSERT_TRUE(runtime->session().project().input_actions.actions().size() == 3);
}

void test_play_simulation() {
  const Outcome a = simulate(600);
  std::printf("x=%.1f y=%.1f cam=%.1f score='%s' banner='%s' health=%.2f coins=%zu hurts=%zu run=%d part=%d\n",
              a.player_x, a.player_y, a.camera_x, a.score_text.c_str(), a.banner_text.c_str(), a.health,
              a.coin_cues, a.hurt_cues, a.saw_run_clip, a.saw_particles);
  ARCANE_ASSERT_TRUE(a.steps_run == 600);
  ARCANE_ASSERT_TRUE(a.player_x > 300.0f);                 // actually ran along the level
  ARCANE_ASSERT_TRUE(std::isfinite(a.player_y));
  ARCANE_ASSERT_TRUE(a.coin_cues >= 1);                    // coin picked up -> sfx cue
  ARCANE_ASSERT_TRUE(a.score_text == "SCORE " + std::to_string(10 * a.coin_cues));
  ARCANE_ASSERT_TRUE(a.score_text != "SCORE 0");           // HUD score updated by gameplay
  ARCANE_ASSERT_TRUE(a.saw_run_clip);                      // animation clip switched to "run"
  ARCANE_ASSERT_TRUE(a.saw_flip);                          // facing right: Sprite::flip_x false
  ARCANE_ASSERT_TRUE(a.saw_particles);                     // pickup burst
  ARCANE_ASSERT_TRUE(a.camera_x >= 400.0f && a.camera_x <= 880.0f);  // Camera2D follows inside its 0..1280 bounds
  ARCANE_ASSERT_TRUE(a.camera_x > 450.0f);                            // and actually left the start position
  ARCANE_ASSERT_TRUE(a.health >= 0.0f && a.health <= 1.0f);

  const Outcome b = simulate(600);  // fixed step + fixed seeds: identical result
  ARCANE_ASSERT_TRUE(a.player_x == b.player_x && a.player_y == b.player_y);
  ARCANE_ASSERT_TRUE(a.score_text == b.score_text && a.health == b.health);
}

// Never jumping: the spikes hurt (and bounce the player over the pit). Without spikes the pit kills.
void test_spikes_and_pit() {
  const Outcome hit = simulate(600, /*jump=*/false);
  ARCANE_ASSERT_TRUE(hit.hurt_cues == 1);
  ARCANE_ASSERT_TRUE(hit.health == 0.75f);

  const Outcome fell = simulate(600, /*jump=*/false, /*spikes=*/false);
  ARCANE_ASSERT_TRUE(fell.health == 0.0f);
  ARCANE_ASSERT_TRUE(fell.banner_text == "GAME OVER");
  ARCANE_ASSERT_TRUE(fell.hurt_cues == 2);  // first fall respawns at a checkpoint, the second ends the game
  // Checkpoint0 (x=590) is passed before the pit, so the respawn is there and not at the start (x=64).
  ARCANE_ASSERT_TRUE(fell.respawn_x > 540.0f && fell.respawn_x < 620.0f);
}

// Pickups, hazards, checkpoints and the goal are physics sensors that report through SliceTrigger.
void test_triggers_are_sensors() {
  auto runtime = arcane::editor::ProjectRuntime::open(ARCANE_SLICE_PROJECT_DIR, ARCANE_SLICE_MODULE_PATH);
  ARCANE_ASSERT_TRUE(runtime.has_value());
  arcane::Node* level = runtime->session().scene().roots().front().get();
  int triggers = 0, checkpoints = 0;
  for (const char* name : {"Coin0", "Coin1", "Coin2", "Coin3", "Coin4", "Spikes", "Goal", "Checkpoint0", "Checkpoint1"}) {
    arcane::Node* n = find(level, name);
    ARCANE_ASSERT_TRUE(n);
    auto* body = static_cast<arcane::RigidBody2D*>(n->find_component("RigidBody2D"));
    auto* collider = static_cast<arcane::Collider2D*>(n->find_component("Collider2D"));
    ARCANE_ASSERT_TRUE(body && collider && n->find_component("SliceTrigger"));
    ARCANE_ASSERT_TRUE(body->body_type == static_cast<int>(arcane::BodyType::Static) && collider->is_sensor);
    ++triggers;
    checkpoints += std::string(name).starts_with("Checkpoint");
  }
  ARCANE_ASSERT_TRUE(triggers == 9 && checkpoints == 2);

  // Coins are collected by trigger events: the score is exactly 10 per coin cue even though the module no
  // longer tests any boxes itself.
  const Outcome run = simulate(300);
  ARCANE_ASSERT_TRUE(run.coin_cues >= 1);
  ARCANE_ASSERT_TRUE(run.score_text == "SCORE " + std::to_string(10 * run.coin_cues));
}

// A small Play world for the animation and enemy tests: the real project and module, the player and enemies placed
// directly (before the physics world is built, as the sensor and body positions are read then), no recorded input.
struct Fixture {
  explicit Fixture(arcane::Vec2 player_at = {64.0f, 340.0f}) {
    auto opened = arcane::editor::ProjectRuntime::open(ARCANE_SLICE_PROJECT_DIR, ARCANE_SLICE_MODULE_PATH);
    ARCANE_ASSERT_TRUE(opened.has_value());
    runtime = std::make_unique<arcane::editor::ProjectRuntime>(std::move(*opened));
    scene = &runtime->session().scene();
    hud = runtime->session().project().hud;
    arcane::set_active_hud(&hud);
    auto created = arcane::AudioSystem::create(true);
    ARCANE_ASSERT_TRUE(created.has_value());
    audio = std::make_unique<arcane::AudioSystem>(std::move(*created));
    arcane::set_active_audio_system(audio.get());
    context = std::make_unique<arcane::GameContext>(ARCANE_SLICE_PROJECT_DIR, runtime->registry());
    arcane::set_active_game_context(context.get());
    level = scene->roots().front().get();
    player = find(level, "Player");
    ARCANE_ASSERT_TRUE(player);
    player->local_transform().position = player_at;
    input = runtime->session().project().input_actions;
    arcane::set_active_input_map(&input);
  }
  ~Fixture() {
    arcane::set_active_input_map(nullptr);
    arcane::set_active_game_context(nullptr);
    arcane::set_active_audio_system(nullptr);
    arcane::set_active_hud(nullptr);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;

  // Builds the physics world; call after placing nodes.
  void start() { physics.rebuild(*scene); }
  void step(bool jump = false, bool right = false) {
    keys.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kD, right);
    keys.set_down(arcane::InputDevice::Keyboard, arcane::Keys::kSpace, jump);
    input.update(keys);
    runtime->module_api()->on_update(kDt);
    scene->update(kDt);
    physics.step(*scene, kDt);
  }
  arcane::Node* named(const char* name) { return find(level, name); }
  arcane::Vec2 pos(const char* name) { return named(name)->world_transform().position; }
  // Removes a node that would get in the way of a test.
  void park(const char* name) { named(name)->set_active(false); }

  std::unique_ptr<arcane::editor::ProjectRuntime> runtime;
  arcane::Scene* scene = nullptr;
  arcane::Hud hud;
  std::unique_ptr<arcane::AudioSystem> audio;
  std::unique_ptr<arcane::GameContext> context;
  arcane::PhysicsWorld physics;
  arcane::InputActionMap input;
  arcane::FakeInputBackend keys;
  arcane::Node* level = nullptr;
  arcane::Node* player = nullptr;
};

// The hero's Animator walks idle -> jump -> fall -> idle through a jump, and shows hurt after a hit.
void test_animgraph_jump_and_fall() {
  Fixture f({300.0f, 340.0f});
  f.park("Enemy0");
  f.park("Enemy1");
  f.start();
  auto* animator = static_cast<arcane::Animator*>(f.player->find_component("Animator"));
  ARCANE_ASSERT_TRUE(animator);
  for (int i = 0; i < 30; ++i) f.step();  // settle on the ground
  ARCANE_ASSERT_TRUE(animator->state() == "idle");
  std::vector<std::string> seen{animator->state()};
  for (int i = 0; i < 90; ++i) {
    f.step(/*jump=*/i < 8);
    if (animator->state() != seen.back()) seen.push_back(animator->state());
  }
  const std::vector<std::string> expected{"idle", "jump", "fall", "idle"};
  if (seen != expected) {
    for (const auto& name : seen) std::printf("state %s\n", name.c_str());
  }
  ARCANE_ASSERT_TRUE(seen == expected);
}

// A PatrolEnemy walks between left_x and right_x and turns at each end.
void test_enemy_patrols() {
  Fixture f;
  f.park("Enemy1");
  f.start();
  const float start_x = f.pos("Enemy0").x;
  float min_x = start_x, max_x = start_x;
  int turns = 0;
  float last_dx = 0.0f;
  for (int i = 0; i < 900; ++i) {
    const float before = f.pos("Enemy0").x;
    f.step();
    const float x = f.pos("Enemy0").x;
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    if (x != before) {
      if (last_dx != 0.0f && (x - before) * last_dx < 0.0f) ++turns;
      last_dx = x - before;
    }
  }
  std::printf("enemy x in [%.1f, %.1f] turns=%d\n", min_x, max_x, turns);
  ARCANE_ASSERT_TRUE(max_x > start_x + 20.0f);   // it walked
  ARCANE_ASSERT_TRUE(min_x <= 775.0f && max_x >= 875.0f);  // and reached both bounds (770..880)
  ARCANE_ASSERT_TRUE(min_x >= 765.0f && max_x <= 885.0f);  // without passing them
  ARCANE_ASSERT_TRUE(turns >= 2);
}

// Dropping onto an enemy from above kills it, bounces the player and scores; the player is not hurt.
void test_stomp_kills_enemy() {
  Fixture f;
  f.park("Enemy1");
  const arcane::Vec2 enemy = {800.0f, 386.0f};
  f.named("Enemy0")->local_transform().position = enemy;
  f.player->local_transform().position = {enemy.x, enemy.y - 100.0f};
  f.start();
  const arcane::NodeId enemy_id = f.named("Enemy0")->id();
  auto* body = static_cast<arcane::RigidBody2D*>(f.player->find_component("RigidBody2D"));
  float best_vy = 0.0f;
  for (int i = 0; i < 90; ++i) {
    f.step();
    best_vy = std::min(best_vy, body->linear_velocity.y);
  }
  ARCANE_ASSERT_TRUE(f.scene->find_node(enemy_id) == nullptr);      // destroyed
  ARCANE_ASSERT_TRUE(f.hud.find_text("score")->text == "SCORE 50");  // enemy_value
  ARCANE_ASSERT_TRUE(f.hud.find_bar("health")->fraction == 1.0f);   // no damage
  ARCANE_ASSERT_TRUE(best_vy < -100.0f);                            // bounced
}

// Walking into an enemy from the side hurts the player and leaves the enemy alive.
void test_side_contact_hurts() {
  Fixture f;
  f.park("Enemy1");
  f.named("Enemy0")->local_transform().position = {400.0f, 386.0f};
  f.player->local_transform().position = {330.0f, 376.0f};
  f.start();
  const arcane::NodeId enemy_id = f.named("Enemy0")->id();
  for (int i = 0; i < 120; ++i) f.step(false, /*right=*/true);
  ARCANE_ASSERT_TRUE(f.scene->find_node(enemy_id) != nullptr);
  ARCANE_ASSERT_TRUE(f.hud.find_bar("health")->fraction == 0.75f);
  ARCANE_ASSERT_TRUE(f.hud.find_text("score")->text == "SCORE 0");
}

}  // namespace

int main() {
  test_project_loads_with_authored_hud();
  test_play_simulation();
  test_spikes_and_pit();
  test_triggers_are_sensors();
  test_animgraph_jump_and_fall();
  test_enemy_patrols();
  test_stomp_kills_enemy();
  test_side_contact_hurts();
  return 0;
}
