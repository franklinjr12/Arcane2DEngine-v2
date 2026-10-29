// Small top-down arena used to exercise the public runtime APIs.
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/hud.hpp"
#include "arcane/input.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"

namespace {
arcane::Node* find(arcane::Node& root, const char* name) {
  if (root.name() == name) return &root;
  for (const auto& child : root.children()) if (auto* found = find(*child, name)) return found;
  return nullptr;
}
arcane::Vec2 direction(float x, float y) {
  const float length = std::sqrt(x*x + y*y);
  return length > 0.001f ? arcane::Vec2{x/length, y/length} : arcane::Vec2{};
}
class ArenaRules : public arcane::Component {
 public:
  float player_speed = 120.0f;
  float enemy_speed = 65.0f;
  float hit_radius = 38.0f;
  float hit_cooldown = 0.5f;
  int hit_damage = 1;
  void on_update(float dt) override {
    auto* player = find(*node(), "Player");
    auto* enemy = find(*node(), "Enemy");
    auto* burst = find(*node(), "HitBurst");
    if (!player || !enemy) return;
    auto* pbody = static_cast<arcane::RigidBody2D*>(player->find_component("RigidBody2D"));
    auto* ebody = static_cast<arcane::RigidBody2D*>(enemy->find_component("RigidBody2D"));
    if (!pbody || !ebody) return;
    float x = 0.0f, y = 0.0f;
    if (auto* input = arcane::active_input_map()) {
      x = static_cast<float>(input->held("right")) - static_cast<float>(input->held("left"));
      y = static_cast<float>(input->held("down")) - static_cast<float>(input->held("up"));
    }
    const auto move = direction(x, y);
    pbody->linear_velocity = {move.x * player_speed, move.y * player_speed};
    const auto pp = player->world_transform().position;
    const auto ep = enemy->world_transform().position;
    const float dx = pp.x - ep.x, dy = pp.y - ep.y;
    const auto chase = direction(dx, dy);
    ebody->linear_velocity = {chase.x * enemy_speed, chase.y * enemy_speed};
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    if (dx*dx + dy*dy <= hit_radius*hit_radius && cooldown_ <= 0.0f) {
      health_ = std::max(0, health_ - std::max(0, hit_damage));
      cooldown_ = std::max(0.0f, hit_cooldown);
      if (burst) {
        burst->local_transform().position = pp;
        if (auto* emitter = static_cast<arcane::ParticleEmitter*>(burst->find_component("ParticleEmitter"))) emitter->burst(12);
      }
    }
    if (auto* hud = arcane::active_hud()) {
      for (auto& element : hud->elements()) {
        if (auto* text = std::get_if<arcane::HudText>(&element); text && text->id == "health_text")
          text->text = std::format("HEALTH {}", health_);
        if (auto* bar = std::get_if<arcane::HudProgressBar>(&element); bar && bar->id == "health_bar")
          bar->fraction = static_cast<float>(health_) / 5.0f;
      }
    }
  }
 private:
  int health_ = 5;
  float cooldown_ = 0.0f;
};
void register_types(arcane::ComponentRegistry& registry) {
  using arcane::PropertyType;
  registry.register_component("ArenaRules", [] { return std::make_unique<ArenaRules>(); }, {
    arcane::make_property<ArenaRules>("player_speed", PropertyType::Float, &ArenaRules::player_speed),
    arcane::make_property<ArenaRules>("enemy_speed", PropertyType::Float, &ArenaRules::enemy_speed),
    arcane::make_property<ArenaRules>("hit_radius", PropertyType::Float, &ArenaRules::hit_radius),
    arcane::make_property<ArenaRules>("hit_cooldown", PropertyType::Float, &ArenaRules::hit_cooldown),
    arcane::make_property<ArenaRules>("hit_damage", PropertyType::Int, &ArenaRules::hit_damage)});
}
void noop() {}
void update(float) {}
const arcane::GameApi api{.api_version=arcane::kGameApiVersion, .game_name="topdown_slice_game",
  .register_types=&register_types, .on_load=&noop, .on_unload=&noop, .on_update=&update, .on_render=&noop};
}
ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t version) {
  return version == arcane::kGameApiVersion ? &api : nullptr;
}
