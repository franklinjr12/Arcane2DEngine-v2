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

}  // namespace arcane::editor
