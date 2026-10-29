#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"

namespace arcane {

// Text-level migrations: legacy (format_version 1) or v2 text in, canonical
// v2 text out. Strict: unknown fields/properties, unregistered component
// types, future versions and malformed input are errors, so nothing is
// silently dropped. The result is verified to reload and re-serialize to
// the same bytes. Input that is already canonical v2 comes back unchanged.
ARCANE_API std::expected<std::string, std::string> migrate_project_json(const std::string& text);
ARCANE_API std::expected<std::string, std::string> migrate_scene_json(const std::string& text,
                                                                      const ComponentRegistry& registry);

struct MigrateOptions {
  bool dry_run = false;  // compute and report everything, write nothing
  // Test hook: make the commit of the Nth changed file fail (-1 = never) to
  // exercise rollback.
  int fail_commit_index = -1;
};

struct MigratedFile {
  std::string path;  // relative to the project directory, '/' separators
  bool changed = false;
  std::string old_text;
  std::string new_text;
};

struct MigrationReport {
  std::vector<MigratedFile> files;  // project.json, scenes sorted by path, then tileset/clip files sorted by path
  bool applied = false;             // false for dry runs and when nothing changed
};

// Migrates <project_dir>/project.json (with its nested input and HUD
// documents), every *.scene.json below the project directory (the main scene
// must exist) and every tileset/clip JSON file below it (recognized by
// content, since they may have any *.json name; files are never renamed). All files are validated
// before any is written; writes are staged and committed together and
// rolled back on failure, so the project is either fully migrated or
// untouched. `registry` must contain the builtin types and the project
// module's types. Idempotent: migrating a v2 project changes nothing.
ARCANE_API std::expected<MigrationReport, std::string> migrate_project(const std::filesystem::path& project_dir,
                                                                       const ComponentRegistry& registry,
                                                                       const MigrateOptions& options = {});

}  // namespace arcane
