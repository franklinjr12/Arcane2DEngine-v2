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
#include "arcane/physics.hpp"

namespace {

class SliceRules : public arcane::Component {
 public:
  float win_x = 1000.0f;
  float pit_y = 700.0f;
  int coin_value = 10;
  float hurt_cooldown = 1.0f;
  int enemy_value = 50;

  int score() const noexcept { return score_; }
  float health() const noexcept { return health_; }
  bool won() const noexcept { return won_; }
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
      if (player_->world_transform().position.x > win_x) won_ = true;  // fallback if the goal node is missing
    }

    animate(*player_);
    refresh_hud(hud);
  }

 private:
  // Restart reloads the authored scene (coins, player, this component) through the runner; without a game
  // context only the counters can be reset.
  void restart() {
    if (auto* context = arcane::active_game_context()) {
      context->request_restart();
      return;
    }
    score_ = 0;
    health_ = 1.0f;
    won_ = false;
    hurt_timer_ = 0.0f;
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
    if (auto* text = hud->find_text("banner")) text->text = won_ ? "YOU WIN" : (dead() ? "GAME OVER" : "");
    if (auto* bar = hud->find_bar("health")) bar->fraction = health_;
  }

  static constexpr float kStompHeight = 24.0f;      // player centre this far above the enemy centre = on top
  static constexpr float kHurtPoseSeconds = 0.3f;   // the hero shows the hurt pose this long after a hit

  int steps_ = 0;
  int stomps_ = 0;
  int score_ = 0;
  float health_ = 1.0f;
  float hurt_timer_ = 0.0f;
  bool won_ = false;
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
