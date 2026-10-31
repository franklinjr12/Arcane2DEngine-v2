// Top-down arena glue. Movement, pursuit, damage and pooling come from the engine components in the scene
// (TopDownController2D, SteeringAgent2D, Health/Hurtbox/Hitbox/Projectile, Spawner2D); this component only wires
// the fire button, the HUD, the hit burst and the restart on death.
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include "arcane/combat.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/game_context.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/navigation.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
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
class ArenaRules : public arcane::Component {
 public:
  float player_speed = 120.0f;  // TopDownController2D::max_speed of Player
  float enemy_speed = 65.0f;    // SteeringAgent2D::max_speed of every agent in the arena
  float bullet_speed = 360.0f;  // Projectile::launch speed
  void on_start() override {
    auto* player = find(*node(), "Player");
    controller_ = component<arcane::TopDownController2D>(player, "TopDownController2D");
    health_ = component<arcane::Health>(player, "Health");
    spawner_ = component<arcane::Spawner2D>(find(*node(), "BulletSpawner"), "Spawner2D");
    burst_ = find(*node(), "HitBurst");
    player_ = player;
    if (controller_) controller_->max_speed = player_speed;
    set_agent_speed(*node(), enemy_speed);
    if (health_) health_->add_listener([this](const arcane::HealthEvent& event) { on_player_health(event); });
  }
  void on_update(float) override {
    auto* input = arcane::active_input_map();
    if (input && input->pressed("fire") && player_ && controller_ && spawner_ && health_ && !health_->dead()) {
      if (auto* bullet = spawner_->spawn(player_->world_transform().position))
        if (auto* projectile = component<arcane::Projectile>(bullet, "Projectile"))
          projectile->launch(controller_->facing(), bullet_speed);
    }
    if (auto* hud = arcane::active_hud(); hud && health_) {
      if (auto* text = hud->find_text("health_text")) text->text = std::format("HEALTH {}", static_cast<int>(std::ceil(health_->current())));
      if (auto* bar = hud->find_bar("health_bar")) bar->fraction = health_->max_health > 0.0f ? health_->current() / health_->max_health : 0.0f;
    }
  }
 private:
  void on_player_health(const arcane::HealthEvent& event) {
    if (event.kind == arcane::HealthEvent::Kind::Healed) return;
    if (burst_ && player_) {
      burst_->local_transform().position = player_->world_transform().position;
      if (auto* emitter = component<arcane::ParticleEmitter>(burst_, "ParticleEmitter")) emitter->burst(12);
    }
    if (event.kind == arcane::HealthEvent::Kind::Died)
      if (auto* context = arcane::active_game_context()) context->request_restart();
  }
  arcane::Node* player_ = nullptr;
  arcane::Node* burst_ = nullptr;
  arcane::TopDownController2D* controller_ = nullptr;
  arcane::Health* health_ = nullptr;
  arcane::Spawner2D* spawner_ = nullptr;
};
void register_types(arcane::ComponentRegistry& registry) {
  using arcane::PropertyType;
  registry.register_component("ArenaRules", [] { return std::make_unique<ArenaRules>(); }, {
    arcane::make_property<ArenaRules>("player_speed", PropertyType::Float, &ArenaRules::player_speed),
    arcane::make_property<ArenaRules>("enemy_speed", PropertyType::Float, &ArenaRules::enemy_speed),
    arcane::make_property<ArenaRules>("bullet_speed", PropertyType::Float, &ArenaRules::bullet_speed)});
}
void noop() {}
void update(float) {}
const arcane::GameApi api{.api_version=arcane::kGameApiVersion, .game_name="topdown_slice_game",
  .register_types=&register_types, .on_load=&noop, .on_unload=&noop, .on_update=&update, .on_render=&noop};
}
ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t version) {
  return version == arcane::kGameApiVersion ? &api : nullptr;
}
