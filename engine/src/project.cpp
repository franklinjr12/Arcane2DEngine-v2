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
                                     "save", "debug", "display", "collision_layers"},
                                     "project");
      } else {
        canonical::require_only_keys(
            root, {"format_version", "name", "main_scene", "module", "input_actions", "hud", "transition",
                         "save", "debug", "display", "collision_layers"},
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
    if (root.contains("display")) {
      const json& d = root["display"];
      canonical::require_object(d, "project 'display'");
      canonical::require_only_keys(d, {"design_width", "design_height", "scale_mode"}, "project 'display'");
      DisplayConfig& c = project.display;
      if (d.contains("design_width")) c.design_width = canonical::as_int(d["design_width"], "display 'design_width'");
      if (d.contains("design_height")) c.design_height = canonical::as_int(d["design_height"], "display 'design_height'");
      if (d.contains("scale_mode")) {
        const std::string mode = canonical::as_string(d["scale_mode"], "display 'scale_mode'");
        if (mode == "letterbox") c.scale_mode = DisplayScaleMode::Letterbox;
        else if (mode == "stretch") c.scale_mode = DisplayScaleMode::Stretch;
        else canonical::fail("project 'display': scale_mode must be \"letterbox\" or \"stretch\"");
      }
      if (!is_valid_design_size(c.design_width, c.design_height)) {
        canonical::fail("project 'display': design_width and design_height must both be 0 or both within 16..8192");
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
    if (root.contains("collision_layers")) {
      const json& layers = root["collision_layers"];
      if (!layers.is_array()) canonical::fail("project 'collision_layers' must be an array");
      for (const json& name : layers) project.collision_layers.push_back(canonical::as_string(name, "collision_layers entry"));
      if (auto ok = validate_collision_layers(project.collision_layers); !ok) canonical::fail("project: " + ok.error());
    }
    return project;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

}  // namespace

bool is_valid_collision_layer_name(const std::string& name) noexcept {
  if (name.empty() || name.size() > 32) return false;
  for (char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

std::expected<void, std::string> validate_collision_layers(const std::vector<std::string>& names) {
  if (names.size() > 31) return std::unexpected(std::string("collision_layers: at most 31 layers"));
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (!is_valid_collision_layer_name(names[i])) {
      return std::unexpected("collision_layers: '" + names[i] + "' must be 1-32 characters of a-z 0-9 _");
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (names[j] == names[i]) return std::unexpected("collision_layers: duplicate name '" + names[i] + "'");
    }
  }
  return {};
}

int collision_layer_index(const ProjectFile& project, const std::string& name) noexcept {
  for (std::size_t i = 0; i < project.collision_layers.size(); ++i) {
    if (project.collision_layers[i] == name) return static_cast<int>(i);
  }
  return -1;
}

bool collision_layer_mask(const ProjectFile& project, const std::vector<std::string>& names, int& mask) noexcept {
  mask = 0;
  for (const std::string& name : names) {
    const int index = collision_layer_index(project, name);
    if (index < 0) return false;
    mask |= 1 << index;
  }
  return true;
}

bool is_valid_design_size(int width, int height) noexcept {
  if (width == 0 && height == 0) return true;
  return width >= 16 && width <= 8192 && height >= 16 && height <= 8192;
}

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
  const DisplayConfig default_display;
  if (project.display.design_width != default_display.design_width ||
      project.display.design_height != default_display.design_height ||
      project.display.scale_mode != default_display.scale_mode) {
    if (!is_valid_design_size(project.display.design_width, project.display.design_height)) {
      return std::unexpected(std::string("project display: design_width and design_height must both be 0 or both within 16..8192"));
    }
    root["display"] = {{"design_width", project.display.design_width},
                       {"design_height", project.display.design_height},
                       {"scale_mode", project.display.scale_mode == DisplayScaleMode::Stretch ? "stretch" : "letterbox"}};
  }
  if (!project.save.game_id.empty() || project.save.data_version != 1) {
    if (!project.save.game_id.empty() && !is_valid_game_id(project.save.game_id)) {
      return std::unexpected(std::string("project save: invalid game_id"));
    }
    if (project.save.data_version < 1) return std::unexpected(std::string("project save: data_version must be >= 1"));
    root["save"] = {{"game_id", project.save.game_id}, {"data_version", project.save.data_version}};
  }
  if (!project.collision_layers.empty()) {
    if (auto ok = validate_collision_layers(project.collision_layers); !ok) return std::unexpected("project " + ok.error());
    root["collision_layers"] = project.collision_layers;
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
