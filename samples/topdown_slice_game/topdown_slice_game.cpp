// Top-down arena glue. Movement, pursuit, damage and pooling come from the engine components in the scene
// (TopDownController2D, SteeringAgent2D, Health/Hurtbox/Hitbox/Projectile, Spawner2D); this component only wires
// the fire button, the waves (deterministic spawn points from a seeded LCG), coin drops, the HUD, the hit burst,
// pause, and the game-over screen that ends in a restart.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <vector>
#include "arcane/combat.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/game_context.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/navigation.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/scene.hpp"
#include "arcane/spawner.hpp"
#include "arcane/topdown.hpp"

namespace {
arcane::Node* find(arcane::Node& root, const char* name) {
  if (root.name() == name) return &root;
  for (const auto& child : root.children()) if (auto* found = find(*child, name)) return found;
  return nullptr;
}
template <class T>
T* component(arcane::Node* node, const char* type) {
  return node ? static_cast<T*>(node->find_component(type)) : nullptr;
}
void set_agent_speed(arcane::Node& node, float speed) {
  if (auto* agent = component<arcane::SteeringAgent2D>(&node, "SteeringAgent2D")) agent->max_speed = speed;
  for (const auto& child : node.children()) set_agent_speed(*child, speed);
}
constexpr arcane::Vec2 kSpawnPoints[] = {{576, 64}, {576, 240}, {576, 416}, {448, 64}, {448, 416}, {352, 64},
                                         {352, 416}, {224, 64}, {224, 416}, {64, 64}, {64, 416}};
constexpr float kMinSpawnDistance = 160.0f;  // enemies never appear closer than this to the player

class ArenaRules : public arcane::Component {
 public:
  float player_speed = 120.0f;  // TopDownController2D::max_speed of Player
  float enemy_speed = 65.0f;    // SteeringAgent2D::max_speed of every agent in the arena
  float bullet_speed = 360.0f;  // Projectile::launch speed
  float wave_duration = 20.0f;  // seconds a wave lasts; the enemies still alive are then released
  float intermission = 3.0f;    // seconds between waves
  int base_enemies = 3;         // enemies in wave 1
  int enemies_per_wave = 2;     // extra enemies per following wave
  int spawn_seed = 12345;       // seed of the LCG that picks spawn points
  float restart_delay = 3.0f;   // seconds of game over before the arena restarts by itself

  int wave() const noexcept { return wave_; }
  bool in_wave() const noexcept { return in_wave_; }
  int score() const noexcept { return static_cast<int>(std::lround(score_)); }
  void add_score(float value) { score_ += value; }

  void on_start() override {
    auto* player = find(*node(), "Player");
    controller_ = component<arcane::TopDownController2D>(player, "TopDownController2D");
    health_ = component<arcane::Health>(player, "Health");
    spawner_ = component<arcane::Spawner2D>(find(*node(), "BulletSpawner"), "Spawner2D");
    enemy_spawner_ = component<arcane::Spawner2D>(find(*node(), "EnemySpawner"), "Spawner2D");
    coin_spawner_ = component<arcane::Spawner2D>(find(*node(), "CoinSpawner"), "Spawner2D");
    burst_ = find(*node(), "HitBurst");
    player_ = player;
    if (controller_) controller_->max_speed = player_speed;
    set_agent_speed(*node(), enemy_speed);
    if (health_) health_->add_listener([this](const arcane::HealthEvent& event) { on_player_health(event); });
    rng_ = static_cast<std::uint32_t>(spawn_seed);
    wave_ = 1; in_wave_ = true; phase_time_ = 0.0f; spawned_ = 0; score_ = 0.0f;
    paused_ = false; game_over_ = false; player_died_ = false; game_over_time_ = 0.0f; frozen_.clear();
    // The HUD is project-level and survives a restart, so every element owned here starts over.
    if (auto* hud = arcane::active_hud()) {
      if (auto* text = hud->find_text("status_text")) text->text.clear();
      if (auto* text = hud->find_text("score_text")) text->text = "COINS 0";
      if (auto* text = hud->find_text("wave_text")) text->text = std::format("WAVE 1  {}:{:02}", static_cast<int>(wave_duration) / 60, static_cast<int>(wave_duration) % 60);
      if (health_) {
        if (auto* text = hud->find_text("health_text")) text->text = std::format("HEALTH {}", static_cast<int>(std::ceil(health_->current())));
        if (auto* bar = hud->find_bar("health_bar")) bar->fraction = health_->max_health > 0.0f ? health_->current() / health_->max_health : 0.0f;
      }
    }
  }
  void on_update(float dt) override {
    auto* input = arcane::active_input_map();
    if (player_died_ && !game_over_) enter_game_over();
    if (game_over_) {
      game_over_time_ += dt;
      if ((input && input->pressed("restart")) || game_over_time_ >= restart_delay) {
        if (auto* context = arcane::active_game_context()) context->request_restart();
      }
    } else if (input && input->pressed("pause")) {
      set_paused(!paused_);
    }
    const bool frozen = paused_ || game_over_;
    if (!frozen && input && input->pressed("fire") && player_ && controller_ && spawner_ && health_ && !health_->dead()) {
      if (auto* bullet = spawner_->spawn(player_->world_transform().position))
        if (auto* projectile = component<arcane::Projectile>(bullet, "Projectile"))
          projectile->launch(controller_->facing(), bullet_speed);
    }
    if (!frozen) {
      drop_coins();
      advance_wave(dt);
    }
    if (auto* hud = arcane::active_hud()) {
      if (health_) {
        if (auto* text = hud->find_text("health_text")) text->text = std::format("HEALTH {}", static_cast<int>(std::ceil(health_->current())));
        if (auto* bar = hud->find_bar("health_bar")) bar->fraction = health_->max_health > 0.0f ? health_->current() / health_->max_health : 0.0f;
      }
      if (auto* text = hud->find_text("wave_text")) {
        const int left = static_cast<int>(std::ceil(std::max(0.0f, (in_wave_ ? wave_duration : intermission) - phase_time_)));
        text->text = in_wave_ ? std::format("WAVE {}  {}:{:02}", wave_, left / 60, left % 60)
                              : std::format("BREAK  {}:{:02}", left / 60, left % 60);
      }
      if (auto* text = hud->find_text("score_text")) text->text = std::format("COINS {}", score());
      if (auto* text = hud->find_text("status_text")) {
        if (game_over_) text->text = std::format("GAME OVER  WAVE {}  COINS {}\nPRESS R", wave_, score());
        else text->text = paused_ ? "PAUSED" : "";
      }
    }
  }
 private:
  // Freezing deactivates the arena's live nodes and remembers exactly which ones; parked pool instances were
  // already inactive, so they are never in the list and stay parked on resume.
  void freeze(bool on) {
    if (!on) {
      for (arcane::NodeId id : frozen_) if (auto* frozen = scene()->find_node(id)) frozen->set_active(true);
      frozen_.clear();
      return;
    }
    auto hold = [this](arcane::Node& candidate) {
      if (&candidate == node() || !candidate.active() || candidate.find_component("Camera")) return;
      frozen_.push_back(candidate.id());
      candidate.set_active(false);
    };
    for (const auto& child : node()->children()) hold(*child);
    for (const auto& root : scene()->roots()) hold(*root);
  }
  void set_paused(bool paused) {
    paused_ = paused;
    freeze(paused);
  }
  void enter_game_over() {
    game_over_ = true; game_over_time_ = 0.0f;
    if (paused_) paused_ = false; else freeze(true);
  }
  int enemy_count() const { return std::max(1, base_enemies + (wave_ - 1) * enemies_per_wave); }
  std::uint32_t next_random() { return rng_ = rng_ * 1664525u + 1013904223u; }
  void advance_wave(float dt) {
    if (!enemy_spawner_) return;
    phase_time_ += dt;
    if (!in_wave_) {
      if (phase_time_ < intermission) return;
      ++wave_; in_wave_ = true; phase_time_ = 0.0f; spawned_ = 0;
    }
    const int total = enemy_count();
    const float spacing = wave_duration * 0.8f / static_cast<float>(total);  // all spawned by 80% of the wave
    while (spawned_ < total && phase_time_ >= static_cast<float>(spawned_) * spacing) {
      spawn_enemy();
      ++spawned_;
    }
    if (phase_time_ >= wave_duration) {
      for (arcane::NodeId id : enemies_)
        if (auto* enemy = scene()->find_node(id); enemy && enemy->active()) enemy_spawner_->release(*enemy);
      enemies_.clear();
      in_wave_ = false;
      phase_time_ = 0.0f;
    }
  }
  void spawn_enemy() {
    const auto player = player_ ? player_->world_transform().position : arcane::Vec2{};
    const std::size_t count = std::size(kSpawnPoints);
    const std::size_t first = next_random() >> 16;
    std::size_t best = first % count;
    float best_distance = -1.0f;
    for (std::size_t i = 0; i < count; ++i) {
      const auto point = kSpawnPoints[(first + i) % count];
      const float distance = std::hypot(point.x - player.x, point.y - player.y);
      if (distance > best_distance) { best_distance = distance; best = (first + i) % count; }
      if (distance >= kMinSpawnDistance) { best = (first + i) % count; break; }
    }
    auto* enemy = enemy_spawner_->spawn(kSpawnPoints[best]);
    if (!enemy) return;
    if (auto* agent = component<arcane::SteeringAgent2D>(enemy, "SteeringAgent2D")) agent->max_speed = enemy_speed;
    if (std::find(enemies_.begin(), enemies_.end(), enemy->id()) == enemies_.end()) enemies_.push_back(enemy->id());
    // Pooled enemies are reused, so each NodeId gets its Died listener only once.
    if (std::find(listened_.begin(), listened_.end(), enemy->id()) != listened_.end()) return;
    listened_.push_back(enemy->id());
    if (auto* health = component<arcane::Health>(enemy, "Health"))
      health->add_listener([this, id = enemy->id()](const arcane::HealthEvent& event) {
        if (event.kind != arcane::HealthEvent::Kind::Died) return;
        if (auto* dead = scene()->find_node(id)) pending_coins_.push_back(dead->world_transform().position);
      });
  }
  void drop_coins() {  // spawned here, not inside the Died callback, which runs during the physics step
    if (coin_spawner_) for (const auto& position : pending_coins_) coin_spawner_->spawn(position);
    pending_coins_.clear();
  }
  void on_player_health(const arcane::HealthEvent& event) {
    if (event.kind == arcane::HealthEvent::Kind::Healed) return;
    if (burst_ && player_) {
      burst_->local_transform().position = player_->world_transform().position;
      if (auto* emitter = component<arcane::ParticleEmitter>(burst_, "ParticleEmitter")) emitter->burst(12);
    }
    if (event.kind == arcane::HealthEvent::Kind::Died) player_died_ = true;  // frozen on the next update, not mid-physics
  }
  arcane::Node* player_ = nullptr;
  arcane::Node* burst_ = nullptr;
  arcane::TopDownController2D* controller_ = nullptr;
  arcane::Health* health_ = nullptr;
  arcane::Spawner2D* spawner_ = nullptr;
  arcane::Spawner2D* enemy_spawner_ = nullptr;
  arcane::Spawner2D* coin_spawner_ = nullptr;
  std::uint32_t rng_ = 1;
  int wave_ = 1;
  bool in_wave_ = true;
  float phase_time_ = 0.0f;
  int spawned_ = 0;
  float score_ = 0.0f;
  float game_over_time_ = 0.0f;
  bool paused_ = false, game_over_ = false, player_died_ = false;
  std::vector<arcane::NodeId> enemies_, listened_, frozen_;
  std::vector<arcane::Vec2> pending_coins_;
};
// A pooled coin: the Player touching its sensor collider banks `value` in ArenaRules and returns it to CoinSpawner.
class CoinPickup : public arcane::Component {
 public:
  float value = 1.0f;
  void on_trigger_enter(arcane::Node& other) override {
    if (other.name() != "Player" || !node()->active()) return;
    if (auto* rules = component<ArenaRules>(scene()->find_node_by_name("Arena"), "ArenaRules")) rules->add_score(value);
    if (!arcane::Spawner2D::release_to_pool(*node())) scene()->destroy_node(node()->id());
  }
};
void register_types(arcane::ComponentRegistry& registry) {
  using arcane::PropertyType;
  registry.register_component("ArenaRules", [] { return std::make_unique<ArenaRules>(); }, {
    arcane::make_property<ArenaRules>("player_speed", PropertyType::Float, &ArenaRules::player_speed),
    arcane::make_property<ArenaRules>("enemy_speed", PropertyType::Float, &ArenaRules::enemy_speed),
    arcane::make_property<ArenaRules>("bullet_speed", PropertyType::Float, &ArenaRules::bullet_speed),
    arcane::make_property<ArenaRules>("wave_duration", PropertyType::Float, &ArenaRules::wave_duration),
    arcane::make_property<ArenaRules>("intermission", PropertyType::Float, &ArenaRules::intermission),
    arcane::make_property<ArenaRules>("base_enemies", PropertyType::Int, &ArenaRules::base_enemies),
    arcane::make_property<ArenaRules>("enemies_per_wave", PropertyType::Int, &ArenaRules::enemies_per_wave),
    arcane::make_property<ArenaRules>("spawn_seed", PropertyType::Int, &ArenaRules::spawn_seed),
    arcane::make_property<ArenaRules>("restart_delay", PropertyType::Float, &ArenaRules::restart_delay)});
  registry.register_component("CoinPickup", [] { return std::make_unique<CoinPickup>(); }, {
    arcane::make_property<CoinPickup>("value", PropertyType::Float, &CoinPickup::value)});
}
void noop() {}
void update(float) {}
const arcane::GameApi api{.api_version=arcane::kGameApiVersion, .game_name="topdown_slice_game",
  .register_types=&register_types, .on_load=&noop, .on_unload=&noop, .on_update=&update, .on_render=&noop};
}
ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t version) {
  return version == arcane::kGameApiVersion ? &api : nullptr;
}
