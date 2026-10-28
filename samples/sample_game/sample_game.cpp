// Minimal native C++ game module used to validate the module loading ABI
// (Foundation A) and the component registration/scene-serialization ABI
// (Foundation B).
#include <memory>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/input.hpp"
#include "arcane/log.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"

namespace {
constexpr std::string_view kLogCategory = "sample_game";
float g_time_seconds = 0.0f;

// Example custom component: rotates its node at a constant rate. Exercises
// the module registration API with two editable properties.
class Spinner : public arcane::Component {
 public:
  void on_update(float fixed_dt) override {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    node()->local_transform().rotation_radians += speed_degrees_per_second * kDegToRad * fixed_dt;
  }

  float speed_degrees_per_second = 90.0f;
  std::string label = "spinner";
};

// Reads the "move_left"/"move_right"/"jump" input actions (see
// arcane/input.hpp) and drives the sibling RigidBody2D, demonstrating how
// gameplay code queries the active input map during Play. No-op if there
// is no active input map (e.g. this component's node exists in a scene
// that is never run) or no sibling RigidBody2D.
class PlayerController : public arcane::Component {
 public:
  void on_update(float fixed_dt) override {
    (void)fixed_dt;
    auto* body = static_cast<arcane::RigidBody2D*>(node()->find_component("RigidBody2D"));
    arcane::InputActionMap* input = arcane::active_input_map();
    if (!body || !input) return;

    float velocity_x = 0.0f;
    if (input->held("move_left")) velocity_x -= move_speed;
    if (input->held("move_right")) velocity_x += move_speed;
    body->linear_velocity.x = velocity_x;
    if (input->pressed("jump")) {
      body->linear_velocity.y = -jump_speed;  // y-down: up is negative.
    }
  }

  float move_speed = 160.0f;
  float jump_speed = 320.0f;
};

void register_types(arcane::ComponentRegistry& registry) {
  registry.register_component(
      "Spinner", [] { return std::make_unique<Spinner>(); },
      {
          arcane::make_property<Spinner>("speed_degrees_per_second", arcane::PropertyType::Float,
                                          &Spinner::speed_degrees_per_second),
          arcane::make_property<Spinner>("label", arcane::PropertyType::String, &Spinner::label),
      });
  registry.register_component(
      "PlayerController", [] { return std::make_unique<PlayerController>(); },
      {
          arcane::make_property<PlayerController>("move_speed", arcane::PropertyType::Float,
                                                    &PlayerController::move_speed),
          arcane::make_property<PlayerController>("jump_speed", arcane::PropertyType::Float,
                                                    &PlayerController::jump_speed),
      });
}

void on_load() { arcane::log(arcane::LogLevel::Info, kLogCategory, "sample_game loaded"); }
void on_unload() { arcane::log(arcane::LogLevel::Info, kLogCategory, "sample_game unloaded"); }
void on_update(float delta_seconds) { g_time_seconds += delta_seconds; }
void on_render() {}

const arcane::GameApi g_api{
    .api_version = arcane::kGameApiVersion,
    .game_name = "sample_game",
    .register_types = &register_types,
    .on_load = &on_load,
    .on_unload = &on_unload,
    .on_update = &on_update,
    .on_render = &on_render,
};
}  // namespace

ARCANE_GAME_API const arcane::GameApi* arcane_get_game_api(std::uint32_t engine_api_version) {
  if (engine_api_version != arcane::kGameApiVersion) {
    return nullptr;
  }
  return &g_api;
}
