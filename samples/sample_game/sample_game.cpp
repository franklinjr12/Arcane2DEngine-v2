// Minimal native C++ game module used to validate the module loading ABI
// (Foundation A) and later exercised by the editor/player once Foundation B
// adds scene/component registration.
#include "arcane/game_api.hpp"
#include "arcane/log.hpp"

namespace {
constexpr std::string_view kLogCategory = "sample_game";
float g_time_seconds = 0.0f;

void on_load() { arcane::log(arcane::LogLevel::Info, kLogCategory, "sample_game loaded"); }
void on_unload() { arcane::log(arcane::LogLevel::Info, kLogCategory, "sample_game unloaded"); }
void on_update(float delta_seconds) { g_time_seconds += delta_seconds; }
void on_render() {}

const arcane::GameApi g_api{
    .api_version = arcane::kGameApiVersion,
    .game_name = "sample_game",
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
