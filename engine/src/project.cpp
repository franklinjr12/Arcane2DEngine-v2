#include "arcane/project.hpp"

#include <format>

#include "arcane/migration.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;

constexpr const char* kProjectFormat = "arcane.project";

// input_actions and hud are nested documents with their own format_version
// (migrated to nested format/version by a later job); the project file only
// re-formats them canonically.
json parse_nested(const std::string& text) {
  auto parsed = canonical::parse_strict(text);
  return parsed ? std::move(*parsed) : json::object();
}

// True if everything in `original` is still present, with the same value, in
// `saved` (which may add defaulted fields). Numbers compare as binary32.
bool contains_all(const json& original, const json& saved) {
  if (original.is_object()) {
    if (!saved.is_object()) return false;
    for (auto it = original.begin(); it != original.end(); ++it) {
      if (!saved.contains(it.key()) || !contains_all(it.value(), saved[it.key()])) return false;
    }
    return true;
  }
  if (original.is_array()) {
    if (!saved.is_array() || saved.size() != original.size()) return false;
    for (std::size_t i = 0; i < original.size(); ++i) {
      if (!contains_all(original[i], saved[i])) return false;
    }
    return true;
  }
  if (original.is_number_float() || saved.is_number_float()) {
    return original.is_number() && saved.is_number() &&
           static_cast<float>(original.get<double>()) == static_cast<float>(saved.get<double>());
  }
  return original == saved;
}

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
      auto input_actions = InputActionMap::load_from_json(root["input_actions"].dump());
      if (!input_actions) {
        return std::unexpected(input_actions.error());
      }
      project.input_actions = std::move(*input_actions);
    }
    if (root.contains("hud")) {
      auto hud = Hud::load_from_json(root["hud"].dump());
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
  root["input_actions"] = parse_nested(project.input_actions.save_to_json());
  root["hud"] = parse_nested(project.hud.save_to_json());
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
  // The nested input/HUD loaders are lenient: refuse the migration if they
  // would drop anything the file contains.
  const json original = *canonical::parse_strict(text);
  const json saved = *canonical::parse_strict(*migrated);
  for (const char* nested : {"input_actions", "hud"}) {
    if (original.contains(nested) && !contains_all(original[nested], saved[nested])) {
      return std::unexpected(std::format("project '{}' contains data this build cannot preserve", nested));
    }
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
