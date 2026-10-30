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
        canonical::require_only_keys(root, {"format", "version", "name", "main_scene", "module", "input_actions", "hud", "transition",
                                     "save", "debug"},
                                     "project");
      } else {
        canonical::require_only_keys(
            root, {"format_version", "name", "main_scene", "module", "input_actions", "hud", "transition",
                         "save", "debug"},
            "project");
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
    if (root.contains("transition")) {
      const json& t = root["transition"];
      canonical::require_object(t, "project 'transition'");
      canonical::require_only_keys(t, {"fade_out_seconds", "fade_in_seconds", "color"}, "project 'transition'");
      TransitionConfig& c = project.transition;
      if (t.contains("fade_out_seconds")) c.fade_out_seconds = canonical::as_float(t["fade_out_seconds"], "transition 'fade_out_seconds'");
      if (t.contains("fade_in_seconds")) c.fade_in_seconds = canonical::as_float(t["fade_in_seconds"], "transition 'fade_in_seconds'");
      if (t.contains("color")) c.color = canonical::as_color(t["color"], "transition 'color'");
      if (!(c.fade_out_seconds >= 0.0f && c.fade_out_seconds <= 60.0f) ||
          !(c.fade_in_seconds >= 0.0f && c.fade_in_seconds <= 60.0f)) {
        canonical::fail("project 'transition': fade seconds must be within [0, 60]");
      }
    }
    if (root.contains("debug")) {
      const json& d = root["debug"];
      canonical::require_object(d, "project 'debug'");
      canonical::require_only_keys(d, {"overlay_visible", "overlay_font_scale"}, "project 'debug'");
      if (d.contains("overlay_visible")) project.debug.overlay_visible = canonical::as_bool(d["overlay_visible"], "debug 'overlay_visible'");
      if (d.contains("overlay_font_scale")) project.debug.overlay_font_scale = canonical::as_float(d["overlay_font_scale"], "debug 'overlay_font_scale'");
      if (!(project.debug.overlay_font_scale > 0.0f && project.debug.overlay_font_scale <= 8.0f)) {
        canonical::fail("project 'debug': overlay_font_scale must be within (0, 8]");
      }
    }
    if (root.contains("save")) {
      const json& s = root["save"];
      canonical::require_object(s, "project 'save'");
      canonical::require_only_keys(s, {"game_id", "data_version"}, "project 'save'");
      if (s.contains("game_id")) project.save.game_id = canonical::as_string(s["game_id"], "save 'game_id'");
      if (s.contains("data_version")) project.save.data_version = canonical::as_int(s["data_version"], "save 'data_version'");
      if (!project.save.game_id.empty() && !is_valid_game_id(project.save.game_id)) {
        canonical::fail("project 'save': game_id must be 1-64 characters of A-Z a-z 0-9 . _ -");
      }
      if (project.save.data_version < 1) canonical::fail("project 'save': data_version must be >= 1");
    }
    return project;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

}  // namespace

bool is_valid_game_id(const std::string& id) noexcept {
  if (id.empty() || id.size() > 64 || id == "." || id == "..") return false;
  for (const char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

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
  // Optional sections are written only when they differ from the defaults, so
  // projects that never configure them keep their exact bytes.
  const TransitionConfig default_transition;
  const TransitionConfig& t = project.transition;
  if (t.fade_out_seconds != default_transition.fade_out_seconds ||
      t.fade_in_seconds != default_transition.fade_in_seconds || t.color.r != 0.0f || t.color.g != 0.0f ||
      t.color.b != 0.0f || t.color.a != 1.0f) {
    if (!(t.fade_out_seconds >= 0.0f && t.fade_out_seconds <= 60.0f && t.fade_in_seconds >= 0.0f &&
          t.fade_in_seconds <= 60.0f)) {
      return std::unexpected(std::string("project transition: fade seconds must be within [0, 60]"));
    }
    root["transition"] = {{"fade_out_seconds", t.fade_out_seconds},
                          {"fade_in_seconds", t.fade_in_seconds},
                          {"color", {t.color.r, t.color.g, t.color.b, t.color.a}}};
  }
  const DebugConfig default_debug;
  if (project.debug.overlay_visible != default_debug.overlay_visible ||
      project.debug.overlay_font_scale != default_debug.overlay_font_scale) {
    if (!(project.debug.overlay_font_scale > 0.0f && project.debug.overlay_font_scale <= 8.0f)) {
      return std::unexpected(std::string("project debug: overlay_font_scale must be within (0, 8]"));
    }
    root["debug"] = {{"overlay_visible", project.debug.overlay_visible},
                     {"overlay_font_scale", project.debug.overlay_font_scale}};
  }
  if (!project.save.game_id.empty() || project.save.data_version != 1) {
    if (!project.save.game_id.empty() && !is_valid_game_id(project.save.game_id)) {
      return std::unexpected(std::string("project save: invalid game_id"));
    }
    if (project.save.data_version < 1) return std::unexpected(std::string("project save: data_version must be >= 1"));
    root["save"] = {{"game_id", project.save.game_id}, {"data_version", project.save.data_version}};
  }
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
