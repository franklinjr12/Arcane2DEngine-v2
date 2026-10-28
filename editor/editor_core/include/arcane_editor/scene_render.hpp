#pragma once

#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

#include "arcane/animation.hpp"
#include "arcane/renderer.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/asset_catalog.hpp"

namespace arcane::editor {

// Resolves and caches the GPU textures backing Sprite::texture asset ids.
// Shared by arcane_editor's viewport panel and arcane_player's --project
// mode -- both just need "render a Scene with the sprite renderer", which
// isn't editor-specific.
class TextureCache {
 public:
  void set_assets(std::vector<AssetEntry> assets) { assets_ = std::move(assets); }

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

// Finds the primary Camera in the scene (if any) and renders every Sprite
// via `renderer`, using `textures` to resolve each Sprite::texture asset id
// to a GPU texture (sprites whose texture doesn't resolve are skipped). A
// node that also has an AnimatedSprite has its clip resolved via `clips`
// (if `clips` is null, animation is skipped and the Sprite's default UV is
// used) and draws the UV rect of whichever frame select_frame_index picks
// for that AnimatedSprite's current elapsed_seconds().
void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height,
                   AnimationClipCache* clips = nullptr);

}  // namespace arcane::editor
