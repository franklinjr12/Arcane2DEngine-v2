#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "arcane/audio.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"

namespace arcane {

// What gameplay code needs from the running project beyond its own Scene: the project root (so asset paths do not
// depend on the working directory), asset id lookup, sound playback by asset id, prefab spawning and a scene
// restart request. The player, the editor's Play mode and the headless runners install one while a scene runs
// (set_active_game_context, like active_audio_system); components reach their scene through Component::scene().
class ARCANE_API GameContext {
 public:
  GameContext(std::filesystem::path project_dir, const ComponentRegistry& registry);

  const std::filesystem::path& project_dir() const noexcept { return project_dir_; }
  std::filesystem::path assets_dir() const { return project_dir_ / "assets"; }
  const ComponentRegistry& registry() const noexcept { return *registry_; }

  // Absolute file for an asset id (ids come from asset_id_for_relative_path); empty if no such asset exists.
  std::optional<std::filesystem::path> asset_path(AssetId id);

  // Plays asset `id` through active_audio_system(). False if there is no audio system or no such asset.
  bool play_sound(AssetId id, float volume = 1.0f, bool loop = false, AudioBus bus = AudioBus::Sfx);
  // Same, by path relative to assets/ (e.g. "coin.wav").
  bool play_sound(const std::string& relative_path, float volume = 1.0f, bool loop = false,
                  AudioBus bus = AudioBus::Sfx);

  // Instantiates the project-relative `*.prefab.json` `asset` in `scene` (usually Component::scene()) under
  // `parent`. The new nodes first update on the scene's next tick; physics bodies appear at the next step.
  std::expected<Node*, std::string> spawn_prefab(Scene& scene, const std::string& asset, Node* parent = nullptr);

  // The runner's physics world (null if none): raycast / overlap_aabb / overlap_point for gameplay, see physics.hpp.
  void set_physics(PhysicsWorld* physics) noexcept { physics_ = physics; }
  PhysicsWorld* physics() const noexcept { return physics_; }
  // Collision layer names from project.json; layer_mask turns names into a query mask (false if a name is unknown).
  void set_collision_layers(std::vector<std::string> names) { collision_layers_ = std::move(names); }
  bool layer_mask(const std::vector<std::string>& names, int& mask) const noexcept;

  // The tile at a world position on `layer` of the first Tilemap node (scene order) that has a non-empty tile there.
  struct TileQuery {
    Node* node = nullptr;
    int layer = 0, cx = 0, cy = 0, tile_id = 0;
    bool solid = false;
    std::vector<std::string> tags;
    bool has_tag(const std::string& tag) const noexcept;
  };
  std::optional<TileQuery> tile_at(Scene& scene, Vec2 world, int layer = 0);
  // Same, but only reports tiles that carry `tag` in their tileset ("hazard", "pickup", ...).
  std::optional<TileQuery> tagged_tile_at(Scene& scene, Vec2 world, const std::string& tag, int layer = 0);

  // Asks the runner to reload the authored scene file after the current tick (the runner swaps the scene between
  // ticks, so this is safe from any hook). Everything runtime-made is discarded and on_destroy runs for it.
  void request_restart() noexcept { restart_requested_ = true; }
  bool take_restart_request() noexcept {
    const bool requested = restart_requested_;
    restart_requested_ = false;
    return requested;
  }

 private:
  std::filesystem::path project_dir_;
  const ComponentRegistry* registry_;
  std::unordered_map<AssetId, std::filesystem::path> known_assets_;
  bool restart_requested_ = false;
  PhysicsWorld* physics_ = nullptr;
  std::vector<std::string> collision_layers_;
  std::unordered_map<AssetId, std::optional<Tileset>> tilesets_;
};

ARCANE_API void set_active_game_context(GameContext* context) noexcept;
ARCANE_API GameContext* active_game_context() noexcept;

// Parses the scene file `scene_file` and replaces `scene` with it (what a restart does). On failure `scene` is
// untouched.
ARCANE_API std::expected<void, std::string> reload_scene_file(Scene& scene, const std::filesystem::path& scene_file,
                                                              const ComponentRegistry& registry);

}  // namespace arcane
