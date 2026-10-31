// Headless Play simulation of samples/platformer_slice: opens the project with its
// real game module, then runs N fixed steps with scripted input (no window/GL/audio
// device). Mirrors the arcane_player loop minus rendering.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
  if (!spikes) find(level, "Spikes")->local_transform().position = {-100000.0f, 0.0f};
  auto* anim = static_cast<arcane::AnimatedSprite*>(player->find_component("AnimatedSprite"));
  auto* emitter = static_cast<arcane::ParticleEmitter*>(burst->find_component("ParticleEmitter"));
  ARCANE_ASSERT_TRUE(anim && emitter);
  auto* animator = static_cast<arcane::Animator*>(player->find_component("Animator"));
  auto* sprite = static_cast<arcane::Sprite*>(player->find_component("Sprite"));
  ARCANE_ASSERT_TRUE(animator && sprite);

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
  ARCANE_ASSERT_TRUE(fell.hurt_cues == 2);  // first fall respawns at the start, the second ends the game
}

}  // namespace

int main() {
  test_project_loads_with_authored_hud();
  test_play_simulation();
  test_spikes_and_pit();
  return 0;
}
