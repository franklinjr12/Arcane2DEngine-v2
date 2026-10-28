#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "arcane/property.hpp"

namespace arcane::editor {

// Deterministic AssetId for a project asset, derived from its path relative
// to the project's assets/ directory (FNV-1a 64-bit over the normalized,
// forward-slash relative path string). No separate asset-database file is
// needed for Foundation C: the same relative path always hashes to the same
// id, so a Sprite's saved `texture` property keeps resolving correctly
// across save/reopen (and across machines) without the editor persisting an
// id -> path table. See docs/decisions.md.
AssetId asset_id_for_relative_path(const std::string& relative_path) noexcept;

struct AssetEntry {
  AssetId id = 0;
  std::string relative_path;  // forward-slash, relative to the assets dir
  std::filesystem::path absolute_path;
};

// Lists every regular file under `assets_dir` (recursive) whose extension
// looks like an image (.png/.jpg/.jpeg/.bmp), paired with its asset id.
// Returns an empty vector if assets_dir doesn't exist.
std::vector<AssetEntry> list_image_assets(const std::filesystem::path& assets_dir);

// Same as list_image_assets, but for animation clip assets: any .json file
// under assets_dir (clips are authored as their own JSON files, see
// arcane/animation.hpp's save_clip_to_json/load_clip_from_json).
std::vector<AssetEntry> list_animation_clip_assets(const std::filesystem::path& assets_dir);

// Same as list_image_assets, but for sound files (.wav/.ogg/.mp3/.flac)
// backing AudioSource::clip / AudioSystem::play.
std::vector<AssetEntry> list_audio_assets(const std::filesystem::path& assets_dir);

}  // namespace arcane::editor
