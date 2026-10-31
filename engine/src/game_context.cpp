#include "arcane/game_context.hpp"

#include <algorithm>

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

std::expected<std::filesystem::path, std::string> GameContext::save_dir_for(std::string_view slot) const {
  if (!saves_enabled_) return std::unexpected(std::string("saves are disabled in this run"));
  if (!is_valid_game_id(save_config_.game_id)) return std::unexpected(std::string("project has no valid save.game_id"));
  if (!is_valid_save_slot(slot)) return std::unexpected("invalid save slot '" + std::string(slot) + "'");
  std::filesystem::path dir = save_directory(save_config_.game_id);
  if (dir.empty()) return std::unexpected(std::string("no save directory is known"));
  return dir;
}

std::expected<SaveData, std::string> GameContext::load_save(std::string_view slot) {
  auto dir = save_dir_for(slot);
  if (!dir) return std::unexpected(dir.error());
  if (!std::filesystem::exists(save_file_path(*dir, slot))) {
    SaveData empty;
    empty.data_version = save_config_.data_version;
    return empty;
  }
  return load_save_game(*dir, slot, save_config_.data_version);
}

std::expected<void, std::string> GameContext::write_save(std::string_view slot, const SaveData& data) {
  auto dir = save_dir_for(slot);
  if (!dir) return std::unexpected(dir.error());
  return write_save_game(*dir, slot, data);
}

bool GameContext::layer_mask(const std::vector<std::string>& names, int& mask) const noexcept {
  mask = 0;
  for (const std::string& name : names) {
    const auto it = std::find(collision_layers_.begin(), collision_layers_.end(), name);
    if (it == collision_layers_.end()) return false;
    mask |= 1 << (it - collision_layers_.begin());
  }
  return true;
}

bool GameContext::TileQuery::has_tag(const std::string& tag) const noexcept {
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

namespace {
void collect_tilemaps(const std::vector<std::unique_ptr<Node>>& nodes, std::vector<Node*>& out) {
  for (const auto& node : nodes) {
    if (node->find_component("Tilemap")) out.push_back(node.get());
    collect_tilemaps(node->children(), out);
  }
}
}  // namespace

std::optional<GameContext::TileQuery> GameContext::tile_at(Scene& scene, Vec2 world, int layer) {
  std::vector<Node*> maps;
  collect_tilemaps(scene.roots(), maps);
  for (Node* node : maps) {
    auto* tilemap = dynamic_cast<Tilemap*>(node->find_component("Tilemap"));
    Tilemap::TileHit hit;
    if (!tilemap || !tilemap->tile_at_world(node->world_transform(), world, layer, hit) || hit.tile_id == 0) continue;
    TileQuery query{node, layer, hit.cx, hit.cy, hit.tile_id, false, {}};
    auto cached = tilesets_.find(tilemap->tileset);
    if (cached == tilesets_.end()) {  // load once per tileset id; a missing or broken file is remembered as empty
      std::optional<Tileset> loaded;
      if (auto path = asset_path(tilemap->tileset)) {
        if (auto text = read_text_file(*path)) {
          if (auto tileset = load_tileset_from_json(*text)) loaded = std::move(*tileset);
        }
      }
      cached = tilesets_.emplace(tilemap->tileset, std::move(loaded)).first;
    }
    query.solid = tilemap->is_solid(hit.tile_id);
    if (cached->second && hit.tile_id <= static_cast<int>(cached->second->tiles.size())) {
      const TileInfo& info = cached->second->tiles[static_cast<std::size_t>(hit.tile_id - 1)];
      query.solid = query.solid || info.solid;
      query.tags = info.tags;
    }
    return query;
  }
  return std::nullopt;
}

std::optional<GameContext::TileQuery> GameContext::tagged_tile_at(Scene& scene, Vec2 world, const std::string& tag, int layer) {
  auto query = tile_at(scene, world, layer);
  if (query && query->has_tag(tag)) return query;
  return std::nullopt;
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
