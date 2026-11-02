// Game module for the platformer validation slice (samples/platformer_slice).
// Gameplay lives in SliceRules on the "Level" node (score, health, pit death and respawn, HUD, animation
// parameters). Coins, spikes, checkpoints and the goal are sensor nodes carrying a SliceTrigger, which forwards
// the physics on_trigger_enter of the Player to SliceRules. Enemies are Kinematic bodies with a PatrolEnemy, which
// forwards the Player's on_collision_enter (stomp or hurt). The module deliberately uses only public engine APIs; every awkward
// spot is recorded in docs/slice_platformer_gaps.md.
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/animgraph.hpp"
#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/game_api.hpp"
#include "arcane/hud.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/input.hpp"
#include "arcane/log.hpp"
#include "arcane/physics.hpp"
#include "arcane/save_game.hpp"
#include "arcane/scene_flow.hpp"

namespace {

// The score travels from one level to the next through module-static state rather than a persistent root: the
// Level node (with its SliceRules) is replaced by every scene switch, and a persistent node would drag its whole
// subtree along. The goal stores the score here and the next level's on_start consumes it (so a standalone load of
// any level starts at 0); restarting a level re-stores the score it began with.
int g_carried_score = 0;

constexpr const char* kLog = "platformer_slice";
constexpr const char* kMenuScene = "scenes/menu.scene.json";
constexpr const char* kSaveSlot = "progress";  // key "best_score" (project.json "save" names the game folder)

// Best score from the "progress" save. Saves are off in headless/replay runs (the load then errors), which just
// reads as 0; an error with saves on is logged.
int load_best_score() {
  auto* context = arcane::active_game_context();
  if (!context) return 0;
  auto save = context->load_save(kSaveSlot);
  if (!save) {
    if (context->saves_enabled()) ARCANE_LOG_WARN(kLog, "cannot read the best score: {}", save.error());
    return 0;
  }
  return save->get_int("best_score", 0);
}

// Writes `score` if it beats the stored best; returns the best score afterwards. Save errors are logged, not fatal.
int record_best_score(int score) {
  const int best = load_best_score();
  if (score <= best) return best;
  auto* context = arcane::active_game_context();
  if (!context) return best;
  auto save = context->load_save(kSaveSlot);
  arcane::SaveData data = save ? *save : arcane::SaveData{};
  data.set("best_score", score);
  if (auto written = context->write_save(kSaveSlot, data); !written) {
    if (context->saves_enabled()) ARCANE_LOG_WARN(kLog, "cannot write the best score: {}", written.error());
    return best;
  }
  return score;
}

arcane::HudButton make_button(const char* id, const char* label, float offset_y) {
  arcane::HudButton button;
  button.id = id;
  button.label = label;
  button.anchor = arcane::HudAnchor::Center;
  button.offset_y = offset_y;
  button.width = 200.0f;
  button.height = 40.0f;
  return button;
}

constexpr const char* kResumeId = "pause_resume";
constexpr const char* kRestartId = "pause_restart";
constexpr const char* kMenuId = "pause_menu";

class SliceRules : public arcane::Component {
 public:
  float win_x = 1000.0f;
  float pit_y = 700.0f;
  int coin_value = 10;
  float hurt_cooldown = 1.0f;
  int enemy_value = 50;
  std::string next_scene;  // project-relative scene to switch to on the goal; empty = last level (YOU WIN)

  int score() const noexcept { return score_; }
  float health() const noexcept { return health_; }
  bool won() const noexcept { return won_; }
  bool paused() const noexcept { return paused_; }
  bool dead() const noexcept { return health_ <= 0.0f; }
  int steps() const noexcept { return steps_; }
  // Where a pit fall puts the player back: the start, or the latest checkpoint reached.
  arcane::Vec2 respawn_point() const noexcept { return respawn_; }
  int checkpoint_count() const noexcept { return checkpoints_; }
  int stomps() const noexcept { return stomps_; }

  // "step" events come from the run state of hero.animgraph.json.
  void on_animation_event(const std::string& name) override {
    if (name == "step") ++steps_;
  }

  void on_start() override {
    player_ = find_named(*node(), "Player");
    burst_ = find_named(*node(), "PickupBurst");
    if (player_) respawn_ = player_->world_transform().position;
    score_ = entry_score_ = g_carried_score;
    g_carried_score = 0;
    best_ = load_best_score();
  }

  // Menu buttons are HUD elements this component added: leaving the scene must not leave them behind.
  void on_destroy() override {
    if (paused_) {
      if (auto* context = arcane::active_game_context()) context->set_time_scale(1.0f);
    }
    set_menu_open(false, false);
  }

  // Called by SliceTrigger when the Player enters the sensor `trigger` of the given kind.
  void on_player_trigger(const std::string& kind, arcane::Node& trigger) {
    if (won_ || dead() || trigger.pending_destroy()) return;
    if (kind == "coin") {
      collect_coin(trigger);
    } else if (kind == "spikes") {
      if (hurt_timer_ > 0.0f) return;
      damage(0.25f);
      if (auto* rb = rigid_body()) rb->linear_velocity.y = -250.0f;  // y-down: negative is up
    } else if (kind == "checkpoint") {
      const auto pos = trigger.world_transform().position;
      if (pos.x <= respawn_.x) return;  // only further along than the current one
      respawn_ = {pos.x, pos.y - 40.0f};  // stand on the ground next to the flag
      ++checkpoints_;
      if (auto* flag = static_cast<arcane::Sprite*>(trigger.find_component("Sprite"))) flag->tint = {0.3f, 1.0f, 0.5f, 1.0f};
    } else if (kind == "goal") {
      won_ = true;
      cue("win.wav");
      go_to_next_level();
    }
  }

  // Called by PatrolEnemy when the Player touches it: landing on top (above it and not rising) stomps the enemy
  // and bounces the player; any other contact hurts through the same cooldown path as the spikes.
  void on_player_touch_enemy(arcane::Node& enemy) {
    if (won_ || dead() || enemy.pending_destroy() || !player_) return;
    auto* rb = rigid_body();
    if (!rb) return;
    const arcane::Vec2 p = player_->world_transform().position;
    const arcane::Vec2 e = enemy.world_transform().position;
    if (p.y < e.y - kStompHeight && rb->linear_velocity.y > -1.0f) {
      score_ += enemy_value;
      ++stomps_;
      rb->linear_velocity.y = -300.0f;
      cue("coin.wav");
      scene()->destroy_node(enemy.id());  // deferred to the end of the tick
    } else if (hurt_timer_ <= 0.0f) {
      damage(0.25f);
      rb->linear_velocity = {p.x < e.x ? -200.0f : 200.0f, -250.0f};
    }
  }

  void on_update(float dt) override {
    if (!player_) return;

    arcane::Hud* hud = arcane::active_hud();
    if (hud && hud->clicked("restart")) restart();
    if (hud && hud->contains(kResumeId) && hud->clicked(kResumeId)) set_paused(false);
    else if (hud && hud->contains(kRestartId) && hud->clicked(kRestartId)) restart();
    else if (hud && hud->contains(kMenuId) && hud->clicked(kMenuId)) go_to_menu();
    auto* input = arcane::active_input_map();
    if (input && input->pressed("pause") && !won_ && !dead()) set_paused(!paused_);
    if (dead()) finish_run();
    hurt_timer_ = std::max(0.0f, hurt_timer_ - dt);

    if (!won_ && !dead()) {
      if (player_->world_transform().position.y > pit_y) {
        damage(0.5f);
        if (!dead()) {  // fell into the pit with health left: back to the latest checkpoint
          if (auto* rb = rigid_body()) {
            rb->teleport(respawn_);
            rb->linear_velocity = {};
          }
        }
      }
      if (player_->world_transform().position.x > win_x) {  // fallback if the goal node is missing
        won_ = true;
        go_to_next_level();
      }
    }

    animate(*player_);
    refresh_hud(hud);
  }

 private:
  // Restart reloads the authored scene (coins, player, this component) through the runner; without a game
  // context only the counters can be reset.
  void restart() {
    set_paused(false);
    set_menu_open(false, false);  // a reload does not run on_destroy
    // Under a SceneManager the level being played may not be the project's main scene, which is all the runner's
    // restart reloads, so reload this one through the manager (applied after the current tick, without a fade).
    if (auto* manager = arcane::active_scene_manager(); manager && !manager->current_scene_path().empty()) {
      g_carried_score = entry_score_;
      manager->request_switch(manager->current_scene_path(), false);
      return;
    }
    if (auto* context = arcane::active_game_context()) {
      context->request_restart();
      return;
    }
    score_ = 0;
    health_ = 1.0f;
    won_ = false;
    finished_ = false;
    hurt_timer_ = 0.0f;
  }

  void go_to_next_level() {
    auto* manager = arcane::active_scene_manager();
    if (next_scene.empty() || !manager) {  // the last level: the run is over
      finish_run();
      return;
    }
    if (switching_) return;
    switching_ = true;
    g_carried_score = score_;
    manager->request_switch(next_scene);
  }

  // The run is over (YOU WIN on the last level, or GAME OVER): record the best score and offer Restart and Menu.
  void finish_run() {
    if (finished_) return;
    finished_ = true;
    best_ = record_best_score(score_);
    set_menu_open(true, false);
  }

  void go_to_menu() {
    if (auto* manager = arcane::active_scene_manager()) manager->request_switch(kMenuScene);
  }

  // Pausing is the game context's time scale: the scene keeps updating at dt 0 (so `pause` and the buttons are still
  // read) while physics and every dt-driven component stand still. Without a GameContext there is nothing to scale.
  void set_paused(bool paused) {
    auto* context = arcane::active_game_context();
    if (paused == paused_ || !context) return;
    paused_ = paused;
    context->set_time_scale(paused ? 0.0f : 1.0f);
    set_menu_open(paused, true);
  }

  // The pause / end-of-run menu: a centred column of HUD buttons, removed again on close. The level HUD's own
  // RESTART button is disabled while it is open, so D-pad down (which focuses the first enabled button) lands on the
  // menu's first button rather than the screen corner.
  void set_menu_open(bool open, bool resumable) {
    arcane::Hud* hud = arcane::active_hud();
    if (!hud) return;
    for (const char* id : {kResumeId, kRestartId, kMenuId}) hud->remove(id);
    if (auto* corner = hud->find_button("restart")) corner->enabled = !open;
    if (!open) return;
    float y = 10.0f;
    if (resumable) {
      hud->add(make_button(kResumeId, "RESUME", y));
      y += 50.0f;
    }
    hud->add(make_button(kRestartId, "RESTART", y));
    hud->add(make_button(kMenuId, "MENU", y + 50.0f));
  }

  static arcane::Node* find_named(arcane::Node& root, const std::string& name) {
    for (const auto& child : root.children()) {
      if (child->name() == name) return child.get();
      if (auto* found = find_named(*child, name)) return found;
    }
    return nullptr;
  }

  arcane::RigidBody2D* rigid_body() {
    return player_ ? static_cast<arcane::RigidBody2D*>(player_->find_component("RigidBody2D")) : nullptr;
  }

  void collect_coin(arcane::Node& coin) {
    score_ += coin_value;
    cue("coin.wav");
    if (burst_) {
      burst_->local_transform().position = coin.world_transform().position;
      if (auto* e = static_cast<arcane::ParticleEmitter*>(burst_->find_component("ParticleEmitter"))) {
        e->burst(12);
      }
    }
    scene()->destroy_node(coin.id());  // deferred to the end of the tick
  }

  void damage(float amount) {
    health_ = std::max(0.0f, health_ - amount);
    hurt_timer_ = hurt_cooldown;
    cue("hurt.wav");
  }

  void cue(const char* asset) {
    if (auto* context = arcane::active_game_context()) context->play_sound(std::string(asset));
  }

  // The Animator on the Player (hero.animgraph.json) picks idle/run/jump/fall/hurt from the parameters below; facing
  // is the Sprite's flip_x, which the Collider2D follows (mirror_with_sprite).
  void animate(arcane::Node& player) {
    auto* animator = static_cast<arcane::Animator*>(player.find_component("Animator"));
    auto* sprite = static_cast<arcane::Sprite*>(player.find_component("Sprite"));
    auto* body = static_cast<arcane::RigidBody2D*>(player.find_component("RigidBody2D"));
    if (!body) return;
    if (animator) {
      animator->set_bool("moving", std::abs(body->linear_velocity.x) > 10.0f);
      animator->set_bool("grounded", body->grounded);
      animator->set_float("vy", body->linear_velocity.y);  // y-down: negative while rising
      animator->set_bool("hurt", hurt_timer_ > hurt_cooldown - kHurtPoseSeconds);
    }
    if (sprite && body->linear_velocity.x < -10.0f) sprite->flip_x = true;
    else if (sprite && body->linear_velocity.x > 10.0f) sprite->flip_x = false;
  }

  // HUD elements are found by their authored ids (project.json "hud").
  void refresh_hud(arcane::Hud* hud) {
    if (!hud) return;
    if (auto* text = hud->find_text("score")) text->text = std::format("SCORE {}", score_);
    if (auto* text = hud->find_text("banner")) text->text = won_ ? (switching_ ? "" : "YOU WIN") : (dead() ? "GAME OVER" : (paused_ ? "PAUSED" : ""));
    if (auto* bar = hud->find_bar("health")) bar->fraction = health_;
    if (auto* text = hud->find_text("best")) text->text = best_ > 0 ? std::format("BEST {}", best_) : std::string();
  }

  static constexpr float kStompHeight = 24.0f;      // player centre this far above the enemy centre = on top
  static constexpr float kHurtPoseSeconds = 0.3f;   // the hero shows the hurt pose this long after a hit

  int steps_ = 0;
  int stomps_ = 0;
  int score_ = 0;
  float health_ = 1.0f;
  float hurt_timer_ = 0.0f;
  bool won_ = false;
  bool paused_ = false;
  bool switching_ = false;
  bool finished_ = false;
  int best_ = 0;
  int entry_score_ = 0;  // score when this level began (restart returns to it)
  int checkpoints_ = 0;
  arcane::Vec2 respawn_{};
  arcane::Node* player_ = nullptr;
  arcane::Node* burst_ = nullptr;
};

// On a sensor node (coin, spikes, checkpoint, goal): reports the Player entering it to the Level's SliceRules.
class SliceTrigger : public arcane::Component {
 public:
  std::string kind;  // coin | spikes | checkpoint | goal

  void on_trigger_enter(arcane::Node& other) override {
    if (other.name() != "Player") return;
    for (arcane::Node* n = node()->parent(); n; n = n->parent()) {
      if (auto* rules = static_cast<SliceRules*>(n->find_component("SliceRules"))) {
        rules->on_player_trigger(kind, *node());
        return;
      }
    }
  }
};

// A Kinematic enemy walking between left_x and right_x (world x) that flips its Sprite at each end; reports the
// Player touching it to the Level's SliceRules.
class PatrolEnemy : public arcane::Component {
 public:
  float speed = 50.0f;
  float left_x = 0.0f;
  float right_x = 0.0f;

  float direction() const noexcept { return dir_; }

  void on_update(float) override {
    auto* body = static_cast<arcane::RigidBody2D*>(node()->find_component("RigidBody2D"));
    if (!body) return;
    const float x = node()->world_transform().position.x;
    if (x <= left_x) dir_ = 1.0f;
    else if (x >= right_x) dir_ = -1.0f;
    body->linear_velocity = {dir_ * speed, 0.0f};
    if (auto* sprite = static_cast<arcane::Sprite*>(node()->find_component("Sprite"))) sprite->flip_x = dir_ < 0.0f;
  }

  void on_collision_enter(arcane::Node& other) override {
    if (other.name() != "Player") return;
    for (arcane::Node* n = node()->parent(); n; n = n->parent()) {
      if (auto* rules = static_cast<SliceRules*>(n->find_component("SliceRules"))) {
        rules->on_player_touch_enemy(*node());
        return;
      }
    }
  }

 private:
  float dir_ = 1.0f;
};

// The menu scene (scenes/menu.scene.json): title, best score and a PLAY button, all HUD elements this component adds
// on start and removes on destroy. The level HUD from project.json is set aside while the menu is up and put back
// when the scene is left, so the project's hud stays the level HUD.
class MenuController : public arcane::Component {
 public:
  std::string title = "PLATFORMER SLICE";
  std::string play_scene = "scenes/main.scene.json";

  void on_start() override {
    arcane::Hud* hud = arcane::active_hud();
    if (!hud) return;
    for (const char* id : {"score", "health", "banner", "restart", "best"}) {
      for (const auto& element : hud->elements()) {
        if (element_id(element) == id) {
          stashed_.push_back(element);
          break;
        }
      }
      hud->remove(id);
    }
    arcane::HudText heading;
    heading.id = "menu_title";
    heading.text = title;
    heading.anchor = arcane::HudAnchor::Center;
    heading.offset_y = -90.0f;
    heading.font_scale = 5.0f;
    heading.color = {1.0f, 0.9f, 0.3f, 1.0f};
    hud->add(heading);
    arcane::HudText best;
    best.id = "menu_best";
    best.text = std::format("BEST {}", load_best_score());
    best.anchor = arcane::HudAnchor::Center;
    best.offset_y = -30.0f;
    best.font_scale = 3.0f;
    best.color = {1.0f, 1.0f, 1.0f, 1.0f};
    hud->add(best);
    hud->add(make_button("menu_play", "PLAY", 30.0f));
  }

  void on_update(float) override {
    arcane::Hud* hud = arcane::active_hud();
    auto* manager = arcane::active_scene_manager();
    if (hud && manager && !switching_ && hud->contains("menu_play") && hud->clicked("menu_play")) {
      switching_ = true;
      manager->request_switch(play_scene);
    }
  }

  void on_destroy() override {
    arcane::Hud* hud = arcane::active_hud();
    if (!hud) return;
    for (const char* id : {"menu_title", "menu_best", "menu_play"}) hud->remove(id);
    for (auto& element : stashed_) hud->add(std::move(element));
    stashed_.clear();
  }

 private:
  static const std::string& element_id(const arcane::HudElement& element) {
    return std::visit([](const auto& e) -> const std::string& { return e.id; }, element);
  }

  std::vector<arcane::HudElement> stashed_;
  bool switching_ = false;
};

void register_types(arcane::ComponentRegistry& registry) {
  using arcane::PropertyType;
  registry.register_component(
      "SliceRules", [] { return std::make_unique<SliceRules>(); },
      {
          arcane::make_property<SliceRules>("win_x", PropertyType::Float, &SliceRules::win_x),
          arcane::make_property<SliceRules>("pit_y", PropertyType::Float, &SliceRules::pit_y),
          arcane::make_property<SliceRules>("coin_value", PropertyType::Int, &SliceRules::coin_value),
          arcane::make_property<SliceRules>("hurt_cooldown", PropertyType::Float, &SliceRules::hurt_cooldown),
          arcane::make_property<SliceRules>("enemy_value", PropertyType::Int, &SliceRules::enemy_value),
          arcane::make_property<SliceRules>("next_scene", PropertyType::String, &SliceRules::next_scene),
      });
  registry.register_component(
      "MenuController", [] { return std::make_unique<MenuController>(); },
      {
          arcane::make_property<MenuController>("title", PropertyType::String, &MenuController::title),
          arcane::make_property<MenuController>("play_scene", PropertyType::String, &MenuController::play_scene),
      });
  registry.register_component(
      "SliceTrigger", [] { return std::make_unique<SliceTrigger>(); },
      {arcane::make_property<SliceTrigger>("kind", PropertyType::String, &SliceTrigger::kind)});
  registry.register_component(
      "PatrolEnemy", [] { return std::make_unique<PatrolEnemy>(); },
      {
          arcane::make_property<PatrolEnemy>("speed", PropertyType::Float, &PatrolEnemy::speed),
          arcane::make_property<PatrolEnemy>("left_x", PropertyType::Float, &PatrolEnemy::left_x),
          arcane::make_property<PatrolEnemy>("right_x", PropertyType::Float, &PatrolEnemy::right_x),
      });
}

void on_load() {}
void on_unload() {}
void on_update(float) {}
void on_render() {}

const arcane::GameApi g_api{
    .api_version = arcane::kGameApiVersion,
    .game_name = "platformer_slice_game",
    .register_types = &register_types,
    .on_load = &on_load,
    .on_unload = &on_unload,
    .on_update = &on_update,
    .on_render = &on_render,
};
}  // namespace

ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t engine_api_version) {
  if (engine_api_version != arcane::kGameApiVersion) return nullptr;
  return &g_api;
}
