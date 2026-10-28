#include "arcane_editor/asset_catalog.hpp"

#include <algorithm>

namespace arcane::editor {

AssetId asset_id_for_relative_path(const std::string& relative_path) noexcept {
  // FNV-1a 64-bit.
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (unsigned char c : relative_path) {
    hash ^= c;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

namespace {

std::string lowercase_extension(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext;
}

bool looks_like_image(const std::filesystem::path& path) {
  const std::string ext = lowercase_extension(path);
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

bool looks_like_animation_clip(const std::filesystem::path& path) { return lowercase_extension(path) == ".json"; }

bool looks_like_audio(const std::filesystem::path& path) {
  const std::string ext = lowercase_extension(path);
  return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

std::vector<AssetEntry> list_assets_matching(const std::filesystem::path& assets_dir,
                                              bool (*matches)(const std::filesystem::path&)) {
  std::vector<AssetEntry> entries;
  std::error_code ec;
  if (!std::filesystem::is_directory(assets_dir, ec)) {
    return entries;
  }

  for (const auto& dir_entry : std::filesystem::recursive_directory_iterator(
           assets_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (!dir_entry.is_regular_file(ec) || !matches(dir_entry.path())) {
      continue;
    }
    std::filesystem::path relative = std::filesystem::relative(dir_entry.path(), assets_dir, ec);
    std::string relative_str = relative.generic_string();
    entries.push_back(AssetEntry{
        .id = asset_id_for_relative_path(relative_str),
        .relative_path = relative_str,
        .absolute_path = dir_entry.path(),
    });
  }

  std::sort(entries.begin(), entries.end(),
            [](const AssetEntry& a, const AssetEntry& b) { return a.relative_path < b.relative_path; });
  return entries;
}

}  // namespace

std::vector<AssetEntry> list_image_assets(const std::filesystem::path& assets_dir) {
  return list_assets_matching(assets_dir, looks_like_image);
}

std::vector<AssetEntry> list_animation_clip_assets(const std::filesystem::path& assets_dir) {
  return list_assets_matching(assets_dir, looks_like_animation_clip);
}

std::vector<AssetEntry> list_audio_assets(const std::filesystem::path& assets_dir) {
  return list_assets_matching(assets_dir, looks_like_audio);
}

}  // namespace arcane::editor
