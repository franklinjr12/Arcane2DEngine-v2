#include "arcane/project.hpp"

#include <format>

#include "arcane/migration.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;

constexpr const char* kProjectFormat = "arcane.project";

std::expected<ProjectFile, std::string> load_project(const std::string& text, bool strict_unknown) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) {
    return std::unexpected(std::format("malformed project JSON: {}", parsed.error()));
  }
  try {
    const json& root = *parsed;
    const canonical::Dialect dialect = canonical::detect_dialect(root, kProjectFormat, "project");
    const bool strict = strict_unknown || dialect == canonical::Dialect::V2;
    if (strict) {
      if (dialect == canonical::Dialect::V2) {
        canonical::require_only_keys(root, {"format", "version", "name", "main_scene", "module", "input_actions", "hud"},
                                     "project");
      } else {
        canonical::require_only_keys(
            root, {"format_version", "name", "main_scene", "module", "input_actions", "hud"}, "project");
      }
    }
    if (!root.contains("name") || !root.contains("main_scene")) {
      return std::unexpected(std::string("project JSON is missing 'name' or 'main_scene'"));
    }

    ProjectFile project;
    project.name = canonical::as_string(root["name"], "project 'name'");
    project.main_scene = canonical::as_string(root["main_scene"], "project 'main_scene'");
    project.module = root.contains("module") ? canonical::as_string(root["module"], "project 'module'") : std::string();
    if (strict) {
      canonical::require_project_path(project.main_scene, false, "project 'main_scene'");
      canonical::require_project_path(project.module, true, "project 'module'");
    }
    if (root.contains("input_actions")) {
      auto input_actions = strict ? InputActionMap::load_from_json_strict(root["input_actions"].dump())
                                  : InputActionMap::load_from_json(root["input_actions"].dump());
      if (!input_actions) {
        return std::unexpected(input_actions.error());
      }
      project.input_actions = std::move(*input_actions);
    }
    if (root.contains("hud")) {
      auto hud = strict ? Hud::load_from_json_strict(root["hud"].dump()) : Hud::load_from_json(root["hud"].dump());
      if (!hud) {
        return std::unexpected(hud.error());
      }
      project.hud = std::move(*hud);
    }
    return project;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

}  // namespace

std::expected<std::string, std::string> save_project_canonical_json(const ProjectFile& project) {
  json root;
  root["format"] = kProjectFormat;
  root["version"] = 2;
  root["name"] = project.name;
  root["main_scene"] = project.main_scene;
  root["module"] = project.module;
  // Nested documents carry their own format/version (arcane.input, arcane.hud).
  auto input_actions = project.input_actions.save_canonical_json();
  if (!input_actions) return std::unexpected(input_actions.error());
  auto hud = project.hud.save_canonical_json();
  if (!hud) return std::unexpected(hud.error());
  root["input_actions"] = *canonical::parse_strict(*input_actions);
  root["hud"] = *canonical::parse_strict(*hud);
  return canonical::dump(root);
}

std::string save_project_to_json(const ProjectFile& project) {
  return save_project_canonical_json(project).value_or(std::string());
}

std::expected<ProjectFile, std::string> load_project_from_json(const std::string& text) {
  return load_project(text, false);
}

std::expected<ProjectFile, std::string> load_project_from_json_strict(const std::string& text) {
  return load_project(text, true);
}

std::expected<std::string, std::string> migrate_project_json(const std::string& text) {
  auto project = load_project_from_json_strict(text);
  if (!project) {
    return std::unexpected(project.error());
  }
  auto migrated = save_project_canonical_json(*project);
  if (!migrated) {
    return std::unexpected(migrated.error());
  }
  auto reloaded = load_project_from_json_strict(*migrated);
  if (!reloaded) {
    return std::unexpected("migrated project does not reload: " + reloaded.error());
  }
  auto again = save_project_canonical_json(*reloaded);
  if (!again || *again != *migrated) {
    return std::unexpected(std::string("migrated project is not idempotent"));
  }
  return migrated;
}

}  // namespace arcane
