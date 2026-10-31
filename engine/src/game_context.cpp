#include "arcane/game_context.hpp"

#include "arcane/assets.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/prefab.hpp"

namespace arcane {

namespace {
GameContext* g_active_game_context = nullptr;
}

GameContext::GameContext(std::filesystem::path project_dir, const ComponentRegistry& registry)
    : project_dir_(std::move(project_dir)), registry_(&registry) {}

std::optional<std::filesystem::path> GameContext::asset_path(AssetId id) {
  if (auto it = known_assets_.find(id); it != known_assets_.end() && std::filesystem::exists(it->second)) {
    return it->second;
  }
  auto found = find_asset_path(assets_dir(), id);  // unknown or moved: scan again
  if (found) known_assets_[id] = *found;
  return found;
}

bool GameContext::play_sound(AssetId id, float volume, bool loop, AudioBus bus) {
  AudioSystem* audio = active_audio_system();
  if (!audio) return false;
  auto path = asset_path(id);
  if (!path) return false;
  audio->play(*path, volume, loop, bus);
  return true;
}

bool GameContext::play_sound(const std::string& relative_path, float volume, bool loop, AudioBus bus) {
  return play_sound(asset_id_for_relative_path(relative_path), volume, loop, bus);
}

std::expected<Node*, std::string> GameContext::spawn_prefab(Scene& scene, const std::string& asset, Node* parent) {
  return arcane::spawn_prefab(scene, project_dir_, asset, parent, *registry_);
}

void set_active_game_context(GameContext* context) noexcept { g_active_game_context = context; }
GameContext* active_game_context() noexcept { return g_active_game_context; }

std::expected<void, std::string> reload_scene_file(Scene& scene, const std::filesystem::path& scene_file,
                                                   const ComponentRegistry& registry) {
  auto text = read_text_file(scene_file);
  if (!text) return std::unexpected(text.error());
  auto loaded = Scene::load_from_json(*text, registry);
  if (!loaded) return std::unexpected(loaded.error());
  scene = std::move(*loaded);
  return {};
}

}  // namespace arcane
