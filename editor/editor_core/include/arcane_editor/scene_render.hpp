#pragma once

#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/hud.hpp"
#include "arcane/particles.hpp"
#include "arcane/renderer.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "arcane_editor/asset_catalog.hpp"

namespace arcane::editor {

// Resolves and caches the GPU textures backing Sprite::texture asset ids.
// Shared by arcane_editor's viewport panel and arcane_player's --project
// mode -- both just need "render a Scene with the sprite renderer", which
// isn't editor-specific.
class TextureCache {
 public:
  void set_assets(std::vector<AssetEntry> assets) { assets_ = std::move(assets); }

  // Registers an already-created texture under `id` (tests, generated textures).
  void put(AssetId id, Texture texture) { textures_.insert_or_assign(id, std::move(texture)); }

  // 1x1 white texture for untextured particles; created on first use (needs a GL context).
  Texture* white() {
    if (!white_) {
      const unsigned char pixel[4] = {255, 255, 255, 255};
      auto texture = Texture::load_from_pixels(pixel, 1, 1, TextureFilter::Nearest);
      if (texture) white_ = std::move(*texture);
    }
    return white_ ? &*white_ : nullptr;
  }

  Texture* get(AssetId id) {
    if (id == 0) return nullptr;
    auto it = textures_.find(id);
    if (it != textures_.end()) return &it->second;

    for (const auto& entry : assets_) {
      if (entry.id != id) continue;
      auto loaded = Texture::load_from_file(entry.absolute_path, TextureFilter::Nearest);
      if (!loaded) return nullptr;
      auto [inserted, _] = textures_.emplace(id, std::move(*loaded));
      return &inserted->second;
    }
    return nullptr;
  }

 private:
  std::vector<AssetEntry> assets_;
  std::map<AssetId, Texture> textures_;
  std::optional<Texture> white_;
};

// Resolves and caches the AnimationClip assets backing AnimatedSprite::clip
// asset ids, the same shape as TextureCache. A clip that fails to parse (or
// doesn't resolve to a known asset) is cached as absent so render_scene
// just falls back to the Sprite's default UV.
class AnimationClipCache {
 public:
  void set_assets(std::vector<AssetEntry> assets) { assets_ = std::move(assets); }

  const AnimationClip* get(AssetId id) {
    if (id == 0) return nullptr;
    auto it = clips_.find(id);
    if (it != clips_.end()) return it->second ? &*it->second : nullptr;

    for (const auto& entry : assets_) {
      if (entry.id != id) continue;
      std::ifstream in(entry.absolute_path);
      std::ostringstream ss;
      ss << in.rdbuf();
      auto loaded = load_clip_from_json(ss.str());
      auto [inserted, _] = clips_.emplace(id, loaded ? std::optional(*loaded) : std::nullopt);
      return inserted->second ? &*inserted->second : nullptr;
    }
    clips_.emplace(id, std::nullopt);
    return nullptr;
  }

 private:
  std::vector<AssetEntry> assets_;
  std::map<AssetId, std::optional<AnimationClip>> clips_;
};

// Same shape as AnimationClipCache, for Tileset assets (.json files).
class TilesetCache {
 public:
  void set_assets(std::vector<AssetEntry> assets) { assets_ = std::move(assets); }

  const Tileset* get(AssetId id) {
    if (id == 0) return nullptr;
    auto it = tilesets_.find(id);
    if (it != tilesets_.end()) return it->second ? &*it->second : nullptr;

    for (const auto& entry : assets_) {
      if (entry.id != id) continue;
      std::ifstream in(entry.absolute_path);
      std::ostringstream ss;
      ss << in.rdbuf();
      auto loaded = load_tileset_from_json(ss.str());
      auto [inserted, _] = tilesets_.emplace(id, loaded ? std::optional(*loaded) : std::nullopt);
      return inserted->second ? &*inserted->second : nullptr;
    }
    tilesets_.emplace(id, std::nullopt);
    return nullptr;
  }

 private:
  std::vector<AssetEntry> assets_;
  std::map<AssetId, std::optional<Tileset>> tilesets_;
};

// World-space center and zoom of the scene's primary Camera (origin, zoom 1 if none).
struct CameraView {
  float x = 0.0f, y = 0.0f, zoom = 1.0f;
};
CameraView find_primary_camera(Scene& scene);

// Maps a pixel inside a viewport of `width` x `height` (origin top-left, as
// render_scene draws it) to world coordinates through the primary camera.
Vec2 viewport_to_world(Scene& scene, float px, float py, int width, int height);

// Editor paint tool core: sets the cell under `world` on `layer` to
// `tile_id` (0 erases). Returns true only if a cell actually changed, so
// callers know whether to mark the session dirty.
bool paint_tile_at_world(Tilemap& map, const WorldTransform2D& node_world, Vec2 world, int layer, int tile_id);

// Advances every AnimatedSprite in `scene` by `dt` seconds. Play mode does
// this through Scene::update; the editor calls it in Edit mode to preview
// clips on the (non-ticking) edit scene.
void preview_animations(Scene& scene, float dt);

// Same for every ParticleEmitter (Edit-mode preview; particle state is runtime-only, so this never dirties the scene).
// Play mode simulates them through Scene::update.
void preview_particles(Scene& scene, float dt);
// Clears and restarts every emitter, e.g. when the preview is toggled on or Play ends.
void restart_particles(Scene& scene);

// Finds the primary Camera in the scene (if any) and renders every Sprite
// via `renderer`, using `textures` to resolve each Sprite::texture asset id
// to a GPU texture (sprites whose texture doesn't resolve are skipped). A
// node that also has an AnimatedSprite has its clip resolved via `clips`
// (if `clips` is null, animation is skipped and the Sprite's default UV is
// used) and draws the UV rect of whichever frame select_frame_index picks
// for that AnimatedSprite's current elapsed_seconds().
//
// Every Tilemap node is drawn layer by layer (only the cells inside the
// camera view) using the tileset from `tilesets`; skipped if `tilesets` is
// null or the tileset or its texture doesn't resolve.
//
// Every ParticleEmitter is drawn (before the node's own Sprite, so the sprite covers them) as one sprite-batch quad per particle (texture or
// white square, color/size lerped over life).
//
// If `hud` and `hud_renderer` are both non-null, the HUD is drawn in screen
// space after the world, in the same frame.
void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height,
                   AnimationClipCache* clips = nullptr, HudRenderer* hud_renderer = nullptr,
                   const Hud* hud = nullptr, TilesetCache* tilesets = nullptr);

}  // namespace arcane::editor
