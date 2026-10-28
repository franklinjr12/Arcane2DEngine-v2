// Game module for the platformer validation slice (samples/platformer_slice).
// All gameplay lives in one component, SliceRules, on the "Level" node: coin
// pickups, spike damage, pit death, goal, HUD updates, animation clip choice and
// camera follow. It deliberately uses only public engine APIs; every awkward
// spot is recorded in docs/slice_platformer_gaps.md.
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/audio.hpp"
#include "arcane/builtin_components.hpp"
#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/hud.hpp"
#include "arcane/node.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"

namespace {

struct Box {
  float x, y, half_w, half_h;
};

bool overlaps(const Box& a, const Box& b) {
  return std::abs(a.x - b.x) < a.half_w + b.half_w && std::abs(a.y - b.y) < a.half_h + b.half_h;
}

Box box_of(const arcane::Node& node, float w, float h) {
  const auto p = node.world_transform().position;
  return {p.x, p.y, w * 0.5f, h * 0.5f};
}

void collect(arcane::Node& node, std::vector<arcane::Node*>& out) {
  for (const auto& child : node.children()) {
    out.push_back(child.get());
    collect(*child, out);
  }
}

class SliceRules : public arcane::Component {
 public:
  arcane::AssetId idle_clip = 0;
  arcane::AssetId run_clip = 0;
  float win_x = 1000.0f;
  float pit_y = 700.0f;
  int coin_value = 10;
  float hurt_cooldown = 1.0f;

  int score() const noexcept { return score_; }
  float health() const noexcept { return health_; }
  bool won() const noexcept { return won_; }
  bool dead() const noexcept { return health_ <= 0.0f; }

  void on_update(float dt) override {
    std::vector<arcane::Node*> nodes;
    collect(*node(), nodes);
    arcane::Node* player = nullptr;
    arcane::Node* camera = nullptr;
    arcane::Node* goal = nullptr;
    arcane::Node* burst_node = nullptr;
    for (arcane::Node* n : nodes) {
      if (n->name() == "Player") player = n;
      else if (n->name() == "MainCamera") camera = n;
      else if (n->name() == "Goal") goal = n;
      else if (n->name() == "PickupBurst") burst_node = n;
    }
    if (!player) return;

    arcane::Hud* hud = arcane::active_hud();
    if (hud && hud->clicked("restart")) reset();
    hurt_timer_ = std::max(0.0f, hurt_timer_ - dt);

    if (!won_ && !dead()) {
      const Box body = box_of(*player, 28.0f, 46.0f);
      for (arcane::Node* n : nodes) {
        if (n->pending_destroy()) continue;
        if (n->name().starts_with("Coin") && overlaps(body, box_of(*n, 20.0f, 20.0f))) {
          collect_coin(*n, burst_node);
        } else if (n->name() == "Spikes" && hurt_timer_ <= 0.0f && overlaps(body, box_of(*n, 28.0f, 28.0f))) {
          damage(0.25f);
          if (auto* rb = static_cast<arcane::RigidBody2D*>(player->find_component("RigidBody2D"))) {
            rb->linear_velocity.y = -250.0f;  // y-down: negative is up
          }
        }
      }
      if (player->world_transform().position.y > pit_y) damage(1.0f);
      if (goal && overlaps(body, box_of(*goal, 24.0f, 64.0f))) {
        won_ = true;
        cue("assets/win.wav");
      } else if (player->world_transform().position.x > win_x) {
        won_ = true;  // fallback if the goal node is missing
      }
    }

    animate(*player);
    if (camera) {
      // Follow with a floor on the left edge so the level start fills the view.
      camera->local_transform().position.x = std::max(400.0f, player->world_transform().position.x);
    }
    refresh_hud(hud);
  }

 private:
  void reset() {
    score_ = 0;
    health_ = 1.0f;
    won_ = false;
    hurt_timer_ = 0.0f;
    // Not restored (engine gap): player position/velocity (no teleport), collected coins (no instancing).
  }

  void collect_coin(arcane::Node& coin, arcane::Node* burst_node) {
    score_ += coin_value;
    cue("assets/coin.wav");
    if (burst_node) {
      burst_node->local_transform().position = coin.world_transform().position;
      if (auto* e = static_cast<arcane::ParticleEmitter*>(burst_node->find_component("ParticleEmitter"))) {
        e->burst(12);
      }
    }
    // A component cannot reach its Scene to destroy_node (gap), so park the collected coin far off-screen.
    coin.local_transform().position = {-100000.0f, -100000.0f};
  }

  void damage(float amount) {
    health_ = std::max(0.0f, health_ - amount);
    hurt_timer_ = hurt_cooldown;
    cue("assets/hurt.wav");
  }

  void cue(const char* path) {
    if (auto* audio = arcane::active_audio_system()) audio->play(path, 1.0f, false, arcane::AudioBus::Sfx);
  }

  void animate(arcane::Node& player) {
    auto* anim = static_cast<arcane::AnimatedSprite*>(player.find_component("AnimatedSprite"));
    auto* body = static_cast<arcane::RigidBody2D*>(player.find_component("RigidBody2D"));
    if (!anim || !body) return;
    const arcane::AssetId wanted = std::abs(body->linear_velocity.x) > 10.0f ? run_clip : idle_clip;
    if (anim->clip != wanted) {
      anim->clip = wanted;
      anim->reset();
    }
    // Facing: negative node scale.x is the only mirror mechanism available.
    if (body->linear_velocity.x < -10.0f) player.local_transform().scale.x = -1.0f;
    else if (body->linear_velocity.x > 10.0f) player.local_transform().scale.x = 1.0f;
  }

  // HUD elements have no ids (only buttons do): the first text is the score, the first bar is health, the
  // second text is the banner. See the gap list.
  void refresh_hud(arcane::Hud* hud) {
    if (!hud) return;
    int text_index = 0;
    for (arcane::HudElement& element : hud->elements()) {
      if (auto* text = std::get_if<arcane::HudText>(&element)) {
        if (text_index == 0) text->text = std::format("SCORE {}", score_);
        else if (text_index == 1) text->text = won_ ? "YOU WIN" : (dead() ? "GAME OVER" : "");
        ++text_index;
      } else if (auto* bar = std::get_if<arcane::HudProgressBar>(&element)) {
        bar->fraction = health_;
      }
    }
  }

  int score_ = 0;
  float health_ = 1.0f;
  float hurt_timer_ = 0.0f;
  bool won_ = false;
};

void register_types(arcane::ComponentRegistry& registry) {
  using arcane::PropertyType;
  registry.register_component(
      "SliceRules", [] { return std::make_unique<SliceRules>(); },
      {
          arcane::make_property<SliceRules>("idle_clip", PropertyType::AssetId, &SliceRules::idle_clip),
          arcane::make_property<SliceRules>("run_clip", PropertyType::AssetId, &SliceRules::run_clip),
          arcane::make_property<SliceRules>("win_x", PropertyType::Float, &SliceRules::win_x),
          arcane::make_property<SliceRules>("pit_y", PropertyType::Float, &SliceRules::pit_y),
          arcane::make_property<SliceRules>("coin_value", PropertyType::Int, &SliceRules::coin_value),
          arcane::make_property<SliceRules>("hurt_cooldown", PropertyType::Float, &SliceRules::hurt_cooldown),
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
