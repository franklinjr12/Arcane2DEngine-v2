// Minimal native C++ game module used to validate the module loading ABI
// (Foundation A) and the component registration/scene-serialization ABI
// (Foundation B).
#include <memory>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/game_api.hpp"
#include "arcane/log.hpp"
#include "arcane/node.hpp"

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

void register_types(arcane::ComponentRegistry& registry) {
  registry.register_component(
      "Spinner", [] { return std::make_unique<Spinner>(); },
      {
          arcane::make_property<Spinner>("speed_degrees_per_second", arcane::PropertyType::Float,
                                          &Spinner::speed_degrees_per_second),
          arcane::make_property<Spinner>("label", arcane::PropertyType::String, &Spinner::label),
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
