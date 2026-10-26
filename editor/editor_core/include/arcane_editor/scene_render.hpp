#pragma once

#include <map>
#include <vector>

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

// Finds the primary Camera in the scene (if any) and renders every Sprite
// via `renderer`, using `textures` to resolve each Sprite::texture asset id
// to a GPU texture (sprites whose texture doesn't resolve are skipped).
void render_scene(Scene& scene, SpriteRenderer& renderer, TextureCache& textures, int width, int height);

}  // namespace arcane::editor
