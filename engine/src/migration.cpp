#include "arcane/migration.hpp"

#include "arcane/atomic_save.hpp"

#include <algorithm>
#include <format>
#include <set>

#include "arcane/animation.hpp"
#include "arcane/animgraph.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/tilemap.hpp"
#include "canonical_json.hpp"
#include "arcane/project.hpp"

namespace arcane {

namespace {

std::string relative_path(const std::filesystem::path& file, const std::filesystem::path& dir) {
  return file.lexically_relative(dir).generic_string();
}

bool is_scene_file(const std::filesystem::path& path) {
  const std::string name = path.filename().generic_string();
  return name.size() > 11 && name.ends_with(".scene.json");
}

enum class AssetKind { None, Tileset, Clip, AnimGraph };

// Tileset and clip files have arbitrary *.json names, so they are recognized
// by content: the v2 `format` string, or the legacy keys that only that kind
// has. Any other JSON file is not an engine asset and is left alone.
AssetKind classify_asset(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed || !parsed->is_object()) return AssetKind::None;
  const nlohmann::json& root = *parsed;
  if (root.contains("format")) {
    if (root["format"] == "arcane.tileset") return AssetKind::Tileset;
    if (root["format"] == "arcane.clip") return AssetKind::Clip;
    if (root["format"] == "arcane.animgraph") return AssetKind::AnimGraph;
    return AssetKind::None;
  }
  if (!root.contains("format_version")) return AssetKind::None;
  if (root.contains("tile_width") && root.contains("columns")) return AssetKind::Tileset;
  if (root.contains("frames")) return AssetKind::Clip;
  return AssetKind::None;
}

bool is_asset_candidate(const std::filesystem::path& path) {
  const std::string name = path.filename().generic_string();
  return name.ends_with(".json") && name != "project.json" && !is_scene_file(path);
}

}  // namespace

std::expected<MigrationReport, std::string> migrate_project(const std::filesystem::path& project_dir,
                                                            const ComponentRegistry& registry,
                                                            const MigrateOptions& options) {
  MigrationReport report;

  const std::filesystem::path project_path = project_dir / "project.json";
  auto project_text = read_text_file(project_path);
  if (!project_text) {
    return std::unexpected(project_text.error());
  }
  auto project_migrated = migrate_project_json(*project_text);
  if (!project_migrated) {
    return std::unexpected(std::format("project.json: {}", project_migrated.error()));
  }
  auto project = load_project_from_json_strict(*project_migrated);
  if (!project) {
    return std::unexpected(std::format("project.json: {}", project.error()));
  }
  report.files.push_back(MigratedFile{"project.json", *project_text != *project_migrated, *project_text,
                                      *project_migrated});

  std::set<std::string> scene_paths{project->main_scene};
  std::set<std::string> asset_paths;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(project_dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec) && is_scene_file(it->path())) {
      scene_paths.insert(relative_path(it->path(), project_dir));
    } else if (it->is_regular_file(ec) && is_asset_candidate(it->path())) {
      asset_paths.insert(relative_path(it->path(), project_dir));
    }
  }
  if (ec) {
    return std::unexpected(std::format("failed to scan '{}': {}", project_dir.string(), ec.message()));
  }

  for (const std::string& scene_path : scene_paths) {
    auto text = read_text_file(project_dir / scene_path);
    if (!text) {
      return std::unexpected(text.error());
    }
    auto migrated = migrate_scene_json(*text, registry);
    if (!migrated) {
      return std::unexpected(std::format("{}: {}", scene_path, migrated.error()));
    }
    report.files.push_back(MigratedFile{scene_path, *text != *migrated, *text, *migrated});
  }

  for (const std::string& asset_path : asset_paths) {
    auto text = read_text_file(project_dir / asset_path);
    if (!text) {
      return std::unexpected(text.error());
    }
    const AssetKind kind = classify_asset(*text);
    if (kind == AssetKind::None) continue;
    auto migrated = kind == AssetKind::Tileset ? migrate_tileset_json(*text)
                    : kind == AssetKind::Clip  ? migrate_clip_json(*text)
                                               : migrate_animgraph_json(*text);
    if (!migrated) {
      return std::unexpected(std::format("{}: {}", asset_path, migrated.error()));
    }
    report.files.push_back(MigratedFile{asset_path, *text != *migrated, *text, *migrated});
  }

  const bool any_changed =
      std::any_of(report.files.begin(), report.files.end(), [](const MigratedFile& f) { return f.changed; });
  if (options.dry_run || !any_changed) {
    return report;
  }

  // Commit every changed file as one crash-atomic unit (journal in atomic_save.hpp); on an
  // ordinary failure the files already replaced are put back.
  std::vector<FileWrite> writes;
  for (const MigratedFile& file : report.files) {
    if (file.changed) writes.push_back({project_dir / file.path, file.new_text});
  }
  AtomicSaveOptions save_options;
  save_options.fail_commit_index = options.fail_commit_index;
  if (auto committed = commit_files_atomic(project_dir, writes, save_options); !committed) {
    return std::unexpected(committed.error().starts_with("rolled back: ")
                               ? "migration " + committed.error()
                               : "migration failed: " + committed.error());
  }
  report.applied = true;
  return report;
}

}  // namespace arcane
