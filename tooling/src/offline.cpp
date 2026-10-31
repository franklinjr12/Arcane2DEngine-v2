// File-level `arcane` commands: project/scene files, validate, fmt, migrate, schema, docs
// (docs/cli-reference.md). They need no open scene, so they work on projects that cannot be opened.
#include "arcane_tooling/offline.hpp"
#include "offline_internal.hpp"

#include <algorithm>
#include <fstream>

#include "arcane/atlas.hpp"
#include "arcane/animation.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/migration.hpp"
#include "arcane/prefab.hpp"
#include "arcane/project.hpp"
#include "arcane/tilemap.hpp"

namespace arcane::tooling {

ToolError offline_error(const char* code, std::string message, std::string file, std::string path, std::string hint) {
  return ToolError{code, std::move(message), std::move(file), std::move(path), std::move(hint)};
}

Json offline_object_schema(Json properties, std::initializer_list<const char*> required,
                           std::initializer_list<const char*> positional) {
  Json req = Json::array();
  for (const char* name : required) req.push_back(name);
  Json pos = Json::array();
  for (const char* name : positional) pos.push_back(name);
  Json out = {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
  if (!pos.empty()) out["x-positional"] = pos;
  return out;
}

std::expected<void, ToolError> offline_require_project(const OfflineEnv& env) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(env.project_dir / "project.json", ec)) {
    return std::unexpected(offline_error("PROJECT_NOT_FOUND", "No project.json in " + env.project_dir.string(), "", "",
                                         "Pass --project DIR pointing at a directory containing project.json"));
  }
  return {};
}

namespace {

using Result = std::expected<Json, ToolError>;

ToolError error(const char* code, std::string message, std::string file = {}, std::string path = {},
                std::string hint = {}) {
  return offline_error(code, std::move(message), std::move(file), std::move(path), std::move(hint));
}

Json object_schema(Json properties, std::initializer_list<const char*> required = {},
                   std::initializer_list<const char*> positional = {}) {
  return offline_object_schema(std::move(properties), required, positional);
}

const Json kStringArg = {{"type", "string"}};
const Json kBoolArg = {{"type", "boolean"}};

std::string slash(const std::filesystem::path& path) { return path.generic_string(); }

std::expected<void, ToolError> require_project(const OfflineEnv& env) { return offline_require_project(env); }

std::expected<void, ToolError> check_relative(const std::string& path, const char* arg) {
  if (is_project_relative_path(path)) return {};
  return std::unexpected(error("ARGS_INVALID", "'" + path + "' is not a project-relative '/' path", "", std::string("/") + arg,
                               "Use a path like scenes/level1.scene.json"));
}

std::expected<ProjectFile, ToolError> load_project_strict(const OfflineEnv& env) {
  if (auto ok = require_project(env); !ok) return std::unexpected(ok.error());
  auto text = read_text_file(env.project_dir / "project.json");
  if (!text) return std::unexpected(error("IO_ERROR", text.error(), "project.json"));
  auto project = load_project_from_json_strict(*text);
  if (!project) {
    return std::unexpected(error("SCHEMA_INVALID", project.error(), "project.json", "",
                                 "Fix the file or run arcane migrate (unknown data is never dropped)"));
  }
  return std::move(*project);
}

std::expected<void, ToolError> write_file(const std::filesystem::path& path, const std::string& text, const std::string& shown) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (auto written = write_text_file_atomic(path, text); !written) return std::unexpected(error("IO_ERROR", written.error(), shown));
  return {};
}

bool hidden_component(const std::filesystem::path& relative) {
  for (const auto& part : relative) {
    const std::string name = part.string();
    if (name.size() > 1 && name[0] == '.' && name != "..") return true;
  }
  return false;
}

// Project-relative '/' paths of every *.json file below the project, sorted, skipping hidden folders.
std::vector<std::string> json_files(const std::filesystem::path& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    const auto relative = std::filesystem::relative(it->path(), dir, ec);
    if (hidden_component(relative)) {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec) && it->path().extension() == ".json") out.push_back(slash(relative));
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string format_of(const Json& root) {
  return root.is_object() && root.contains("format") && root["format"].is_string() ? root["format"].get<std::string>() : "";
}

Result project_new(const OfflineEnv&, const Json& args, bool dry_run) {
  const std::filesystem::path dir = args["path"].get<std::string>();
  const std::string name = args.value("name", dir.filename().string().empty() ? dir.parent_path().filename().string() : dir.filename().string());
  if (name.empty()) return std::unexpected(error("ARGS_INVALID", "project name must not be empty", "", "/name"));
  if (std::filesystem::exists(dir / "project.json")) {
    return std::unexpected(error("CONFLICT", "project.json already exists in " + dir.string(), "project.json", "",
                                 "Choose another folder; arcane never overwrites a project"));
  }
  ProjectFile project;
  project.name = name;
  project.main_scene = "scenes/main.scene.json";
  auto project_text = save_project_canonical_json(project);
  if (!project_text) return std::unexpected(error("VALUE_INVALID", project_text.error(), "project.json"));
  auto scene_text = Scene{}.save_canonical_json(ComponentRegistry{});
  if (!scene_text) return std::unexpected(error("VALUE_INVALID", scene_text.error(), project.main_scene));
  if (!dry_run) {
    if (auto ok = write_file(dir / project.main_scene, *scene_text, project.main_scene); !ok) return std::unexpected(ok.error());
    std::error_code ec;
    std::filesystem::create_directories(dir / "assets", ec);
    if (auto ok = write_file(dir / "project.json", *project_text, "project.json"); !ok) return std::unexpected(ok.error());
  }
  return Json{{"path", slash(dir / "project.json")}, {"name", name}, {"main_scene", project.main_scene},
              {"files", Json::array({"project.json", project.main_scene})}};
}

Json settings_json(const ProjectFile& project) {
  return {{"name", project.name}, {"main_scene", project.main_scene}, {"module", project.module}};
}

Result project_info(const OfflineEnv& env, const Json&, bool) {
  if (auto ok = require_project(env); !ok) return std::unexpected(ok.error());
  auto text = read_text_file(env.project_dir / "project.json");
  if (!text) return std::unexpected(error("IO_ERROR", text.error(), "project.json"));
  auto project = load_project_from_json(*text);
  if (!project) return std::unexpected(error("SCHEMA_INVALID", project.error(), "project.json"));
  const Json root = Json::parse(*text, nullptr, false);
  int scenes = 0;
  for (const auto& file : json_files(env.project_dir)) scenes += ends_with(file, ".scene.json") ? 1 : 0;
  std::error_code ec;
  Json info = settings_json(*project);
  info["format"] = format_of(root).empty() ? "legacy-v1" : "v2";
  info["scene_files"] = scenes;
  info["module_exists"] = !project->module.empty() && std::filesystem::is_regular_file(env.project_dir / project->module, ec);
  info["transition"] = {{"fade_out_seconds", project->transition.fade_out_seconds},
                        {"fade_in_seconds", project->transition.fade_in_seconds}};
  info["save"] = {{"game_id", project->save.game_id}, {"data_version", project->save.data_version}};
  info["debug"] = {{"overlay_visible", project->debug.overlay_visible}};
  return info;
}

Result settings_get(const OfflineEnv& env, const Json&, bool) {
  auto project = load_project_strict(env);
  if (!project) return std::unexpected(project.error());
  return settings_json(*project);
}

Result settings_set(const OfflineEnv& env, const Json& args, bool dry_run) {
  auto project = load_project_strict(env);
  if (!project) return std::unexpected(project.error());
  const Json previous = settings_json(*project);
  if (args.contains("name")) {
    if (args["name"].get<std::string>().empty()) return std::unexpected(error("VALUE_INVALID", "name must not be empty", "", "/name"));
    project->name = args["name"].get<std::string>();
  }
  if (args.contains("main_scene")) {
    const std::string scene = args["main_scene"].get<std::string>();
    if (auto ok = check_relative(scene, "main_scene"); !ok) return std::unexpected(ok.error());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(env.project_dir / scene, ec)) {
      return std::unexpected(error("VALUE_INVALID", "scene file '" + scene + "' does not exist", "", "/main_scene", "Create it with scene new"));
    }
    project->main_scene = scene;
  }
  if (args.contains("module")) {
    const std::string module = args["module"].get<std::string>();
    if (!module.empty()) {
      if (auto ok = check_relative(module, "module"); !ok) return std::unexpected(ok.error());
    }
    project->module = module;  // the module may not be built yet, so existence is not required
  }
  const Json now = settings_json(*project);
  auto text = save_project_canonical_json(*project);
  if (!text) return std::unexpected(error("VALUE_INVALID", text.error(), "project.json"));
  if (now != previous && !dry_run) {
    if (auto ok = write_file(env.project_dir / "project.json", *text, "project.json"); !ok) return std::unexpected(ok.error());
  }
  return Json{{"changed", now != previous}, {"previous", previous}, {"settings", now}};
}

Result scene_new(const OfflineEnv& env, const Json& args, bool dry_run) {
  if (auto ok = require_project(env); !ok) return std::unexpected(ok.error());
  const std::string path = args["path"].get<std::string>();
  if (auto ok = check_relative(path, "path"); !ok) return std::unexpected(ok.error());
  if (!ends_with(path, ".scene.json")) {
    return std::unexpected(error("ARGS_INVALID", "scene files end in .scene.json", "", "/path", "Use e.g. scenes/level1.scene.json"));
  }
  if (std::filesystem::exists(env.project_dir / path) && !args.value("overwrite", false)) {
    return std::unexpected(error("CONFLICT", "'" + path + "' already exists", path, "", "Pass --overwrite to replace it"));
  }
  auto text = Scene{}.save_canonical_json(ComponentRegistry{});
  if (!text) return std::unexpected(error("VALUE_INVALID", text.error(), path));
  if (!dry_run) {
    if (auto ok = write_file(env.project_dir / path, *text, path); !ok) return std::unexpected(ok.error());
  }
  return Json{{"scene", path}};
}

Result scene_list(const OfflineEnv& env, const Json&, bool) {
  auto project = load_project_strict(env);
  if (!project) return std::unexpected(project.error());
  Json scenes = Json::array();
  for (const auto& file : json_files(env.project_dir)) {
    if (!ends_with(file, ".scene.json")) continue;
    auto text = read_text_file(env.project_dir / file);
    const Json root = text ? Json::parse(*text, nullptr, false) : Json();
    scenes.push_back({{"path", file}, {"main", file == project->main_scene}, {"format", format_of(root).empty() ? "legacy-v1" : "v2"}});
  }
  return Json{{"scenes", scenes}};
}

// Strict check of one file by kind. Returns the problem, or an empty string.
std::string check_file(const OfflineEnv& env, const ComponentRegistry& registry, const std::string& rel, const std::string& text) {
  auto canonical = canonicalize_json(text);
  if (!canonical) return canonical.error();
  const std::string format = format_of(Json::parse(text, nullptr, false));
  auto problem = [](const auto& result) { return result ? std::string() : std::string(result.error()); };
  if (rel == "project.json") return problem(load_project_from_json_strict(text));
  if (ends_with(rel, ".scene.json")) return problem(Scene::load_from_json_strict(text, registry));
  if (format == "arcane.tileset") return problem(load_tileset_from_json_strict(text));
  if (format == "arcane.clip") return problem(load_clip_from_json_strict(text));
  if (format == "arcane.atlas") return problem(load_atlas_from_json(text));
  if (format == "arcane.prefab") return problem(load_prefab_file(env.project_dir, rel, registry));
  return {};  // other documents: well-formed JSON only
}

Result validate(const OfflineEnv& env, const Json& args, bool) {
  auto context = ProjectContext::open(env.project_dir, env.module_override, env.scene_override);
  if (!context) {
    if (context.error().code != "SCHEMA_INVALID") return std::unexpected(context.error());
    // The project does not open strictly: report that as a problem rather than a command failure.
    return Json{{"ok", false}, {"errors", 1}, {"problems", Json::array({{{"file", context.error().file}, {"message", context.error().message}}})},
                {"warnings", Json::array()}, {"checked", 0}};
  }
  Json problems = Json::array(), warnings = Json::array();
  auto add = [](Json& list, const std::string& file, const std::string& message) { list.push_back({{"file", file}, {"message", message}}); };
  std::vector<std::string> checked;

  if (args.contains("file")) {
    const std::string rel = args["file"].get<std::string>();
    if (auto ok = check_relative(rel, "file"); !ok) return std::unexpected(ok.error());
    auto text = read_text_file(env.project_dir / rel);
    if (!text) return std::unexpected(error("IO_ERROR", text.error(), rel));
    if (auto message = check_file(env, (*context)->registry(), rel, *text); !message.empty()) add(problems, rel, message);
    checked.push_back(rel);
  } else {
    // migrate_project validates project, scenes, tilesets and clips strictly (legacy ones included).
    auto report = migrate_project(env.project_dir, (*context)->registry(), MigrateOptions{true, -1});
    if (!report) {
      add(problems, "", report.error());
    } else {
      for (const auto& file : report->files) {
        checked.push_back(file.path);
        if (file.changed) add(warnings, file.path, "legacy or non-canonical; run arcane migrate");
      }
    }
    for (const auto& rel : json_files(env.project_dir)) {
      if (std::find(checked.begin(), checked.end(), rel) != checked.end()) continue;
      auto text = read_text_file(env.project_dir / rel);
      if (!text) {
        add(problems, rel, text.error());
        continue;
      }
      if (auto message = check_file(env, (*context)->registry(), rel, *text); !message.empty()) add(problems, rel, message);
      else if (!format_of(Json::parse(*text, nullptr, false)).empty() && canonicalize_json(*text).value_or(*text) != *text) {
        add(warnings, rel, "not canonical; run arcane fmt");
      }
      checked.push_back(rel);
    }
  }
  return Json{{"ok", problems.empty()}, {"errors", problems.size()}, {"problems", problems},
              {"warnings", warnings}, {"checked", checked.size()}};
}

Result fmt(const OfflineEnv& env, const Json& args, bool dry_run) {
  if (auto ok = require_project(env); !ok) return std::unexpected(ok.error());
  std::vector<std::string> files;
  if (args.contains("file")) {
    if (auto ok = check_relative(args["file"].get<std::string>(), "file"); !ok) return std::unexpected(ok.error());
    files.push_back(args["file"].get<std::string>());
  } else {
    files = json_files(env.project_dir);
  }
  const bool check = args.value("check", false);
  Json changed = Json::array(), skipped = Json::array();
  for (const auto& rel : files) {
    auto text = read_text_file(env.project_dir / rel);
    if (!text) return std::unexpected(error("IO_ERROR", text.error(), rel));
    const Json root = Json::parse(*text, nullptr, false);
    if (root.is_discarded()) return std::unexpected(error("SCHEMA_INVALID", "malformed JSON", rel));
    if (format_of(root).rfind("arcane.", 0) != 0) {
      skipped.push_back(rel);  // legacy or foreign JSON: arcane migrate handles legacy engine files
      continue;
    }
    auto canonical = canonicalize_json(*text);
    if (!canonical) return std::unexpected(error("SCHEMA_INVALID", canonical.error(), rel));
    if (*canonical == *text) continue;
    changed.push_back(rel);
    if (!check && !dry_run) {
      if (auto ok = write_file(env.project_dir / rel, *canonical, rel); !ok) return std::unexpected(ok.error());
    }
  }
  return Json{{"ok", !(check && !changed.empty())}, {"changed", !changed.empty()}, {"files", changed}, {"skipped_legacy_or_foreign", skipped}};
}

Result migrate(const OfflineEnv& env, const Json&, bool dry_run) {
  if (auto ok = require_project(env); !ok) return std::unexpected(ok.error());
  auto runtime = editor::ProjectRuntime::open(env.project_dir, env.module_override, editor::SceneLoadPolicy::Compatible);
  if (!runtime) {
    return std::unexpected(error(runtime.error().rfind("failed to load project module", 0) == 0 ? "MODULE_LOAD_FAILED" : "SCHEMA_INVALID",
                                 runtime.error()));
  }
  auto report = migrate_project(env.project_dir, runtime->registry(), MigrateOptions{dry_run, -1});
  if (!report) {
    return std::unexpected(error("SCHEMA_INVALID", report.error(), "", "", "Nothing was written; fix the reported file and retry"));
  }
  Json files = Json::array();
  bool any = false;
  for (const auto& file : report->files) {
    files.push_back({{"path", file.path}, {"changed", file.changed}});
    any = any || file.changed;
  }
  return Json{{"changed", any}, {"applied", report->applied}, {"files", files}};
}

Result schema(const OfflineEnv& env, const Json& args, bool) {
  if (args.contains("type")) {
    const std::string name = "arcane." + args["type"].get<std::string>();
    for (const auto& s : embedded_schemas()) {
      if (s.name == name) return Json{{"type", args["type"]}, {"schema", Json::parse(s.text)}};
    }
    return std::unexpected(error("ARGS_INVALID", "unknown format type '" + args["type"].get<std::string>() + "'", "", "/type",
                                 "Run arcane schema to list the types"));
  }
  if (args.contains("command")) {
    const std::string name = args["command"].get<std::string>();
    const CommandSpec* spec = env.registry ? env.registry->find(name) : nullptr;
    if (const auto* offline = find_offline_command(name)) spec = &offline->spec;
    if (!spec) return std::unexpected(error("UNKNOWN_COMMAND", "Unknown command '" + name + "'", "", "/command"));
    return Json{{"command", spec->name}, {"summary", spec->summary}, {"mutating", spec->mutating},
                {"args", spec->args_schema}, {"result", spec->result_schema}};
  }
  if (args.contains("component")) {
    auto runtime = editor::ProjectRuntime::open(env.project_dir, env.module_override, editor::SceneLoadPolicy::Compatible);
    if (!runtime) return std::unexpected(error("MODULE_LOAD_FAILED", runtime.error()));
    const std::string name = args["component"].get<std::string>();
    const auto* props = runtime->registry().properties_of(name);
    if (!props) {
      return std::unexpected(error("COMPONENT_TYPE_UNKNOWN", "component type '" + name + "' is not registered", "", "/component",
                                   "Run arcane component types (is the module built and listed in project.json?)"));
    }
    Json list = Json::array();
    for (const auto& p : *props) list.push_back({{"name", p.name}, {"type", std::string(to_string(p.type))}});
    return Json{{"component", name}, {"properties", list}};
  }
  Json types = Json::array();
  for (const auto& s : embedded_schemas()) types.push_back(std::string(s.name.substr(7)));
  return Json{{"types", types}};
}

Result docs(const OfflineEnv& env, const Json& args, bool) {
  if (!env.registry) return std::unexpected(error("MODULE_FAILURE", "no command registry"));
  const std::string format = args.value("format", "md");
  if (format == "json") {
    Json commands = Json::array();
    for (const CommandSpec* spec : env.registry->list()) commands.push_back({{"command", spec->name}, {"summary", spec->summary}, {"mutating", spec->mutating}});
    for (const auto& c : offline_commands()) commands.push_back({{"command", c.spec.name}, {"summary", c.spec.summary}, {"mutating", c.spec.mutating}});
    return Json{{"commands", commands}};
  }
  return Json{{"format", "md"}, {"text", generate_cli_reference(*env.registry)}};
}

std::vector<OfflineCommand> make_commands() {
  std::vector<OfflineCommand> c;
  c.push_back({{"project.new", "Create a project folder with project.json (format v2), an empty main scene and assets/.",
                object_schema({{"path", kStringArg}, {"name", kStringArg}}, {"path"}, {"path"}), {{"type", "object"}}, true},
               project_new});
  c.push_back({{"project.info", "Describe the project: name, main scene, module, format, scene count, transition/save/debug settings.",
                object_schema({}), {{"type", "object"}}, false},
               project_info});
  c.push_back({{"project.settings_get", "Read the project name, main scene and module path.", object_schema({}), {{"type", "object"}}, false},
               settings_get});
  c.push_back({{"project.settings_set", "Change the project name, main scene (must exist) or module path (empty clears); writes project.json.",
                object_schema({{"name", kStringArg}, {"main_scene", kStringArg}, {"module", kStringArg}}), {{"type", "object"}}, true},
               settings_set});
  c.push_back({{"scene.new", "Create an empty scene file (canonical scene v2); never overwrites unless overwrite is set.",
                object_schema({{"path", kStringArg}, {"overwrite", kBoolArg}}, {"path"}, {"path"}), {{"type", "object"}}, true},
               scene_new});
  c.push_back({{"scene.list", "List the scene files of the project with their format.", object_schema({}), {{"type", "object"}}, false}, scene_list});
  c.push_back({{"validate", "Open the project strictly (module, scenes, tilesets, clips, prefabs, atlases) and report problems; exit 4 if any.",
                object_schema({{"file", kStringArg}}), {{"type", "object"}}, false},
               validate});
  c.push_back({{"fmt", "Rewrite canonical-format JSON files (format header present) in canonical form; --check only reports (exit 4 if a file differs).",
                object_schema({{"file", kStringArg}, {"check", kBoolArg}}), {{"type", "object"}}, true},
               fmt});
  c.push_back({{"migrate", "Migrate every project file to canonical v2 atomically (all or nothing); idempotent; honors --dry-run.",
                object_schema({}), {{"type", "object"}}, true},
               migrate});
  c.push_back({{"schema", "Print a static JSON schema (type), a command's argument schema (command) or a component's registered properties (component); without arguments list the types.",
                object_schema({{"type", kStringArg}, {"command", kStringArg}, {"component", kStringArg}}), {{"type", "object"}}, false},
               schema});
  c.push_back({{"docs", "Print the generated command reference (md, the default) or a command list (json).",
                object_schema({{"format", {{"type", "string"}, {"enum", Json::array({"md", "json"})}}}}), {{"type", "object"}}, false},
               docs});
  for (auto& asset : make_asset_commands()) c.push_back(std::move(asset));
  for (auto& run : make_run_commands()) c.push_back(std::move(run));
  for (auto& render : make_render_commands()) c.push_back(std::move(render));
  for (auto& exported : make_export_commands()) c.push_back(std::move(exported));
  return c;
}

}  // namespace

const std::vector<OfflineCommand>& offline_commands() {
  static const std::vector<OfflineCommand> commands = make_commands();
  return commands;
}

const OfflineCommand* find_offline_command(const std::string& name) {
  for (const auto& c : offline_commands()) {
    if (c.spec.name == name) return &c;
  }
  return nullptr;
}

}  // namespace arcane::tooling
