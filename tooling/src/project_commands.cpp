// project.get / project.set: the project-level properties stored in project.json (name, main_scene, module).
// They edit ProjectFile in memory (session().mutable_project()) and mark the project dirty;
// ProjectContext::save writes project.json. Nothing is reloaded: main_scene and module take effect the
// next time the project is opened.
#include "arcane/atlas.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(std::string message, std::string path, std::string hint = {}) {
  return ToolError{"VALUE_INVALID", std::move(message), "project.json", std::move(path), std::move(hint)};
}

Json schema(Json properties) {
  return {{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
}

Json project_json(const ProjectFile& project) {
  return {{"name", project.name}, {"main_scene", project.main_scene}, {"module", project.module}};
}

}  // namespace

void register_project_commands(CommandRegistry& registry) {
  registry.add({"project.get", "Show the project-level properties: name, main_scene and module (from project.json).",
                schema(Json::object()), Json{{"type", "object"}}, false},
               [](ProjectContext& context, const Json&) -> std::expected<CommandResult, ToolError> {
                 return CommandResult{project_json(context.session().project()), std::nullopt, std::nullopt};
               });

  const Json kString = {{"type", "string"}};
  registry.add(
      {"project.set",
       "Set project-level properties in project.json: `name` (non-empty), `main_scene` (project-relative .json path), "
       "`module` (project-relative path, empty for none). At least one is required. main_scene and module take "
       "effect the next time the project is opened; nothing is reloaded now.",
       schema({{"name", kString}, {"main_scene", kString}, {"module", kString}}), Json{{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        if (!args.contains("name") && !args.contains("main_scene") && !args.contains("module")) {
          return std::unexpected(fail("give at least one of name, main_scene, module", "", "project.get shows the current values"));
        }
        if (args.contains("name") && args["name"].get<std::string>().empty()) {
          return std::unexpected(fail("name must not be empty", "/name"));
        }
        if (args.contains("main_scene")) {
          const std::string scene = args["main_scene"].get<std::string>();
          if (!is_project_relative_path(scene) || !scene.ends_with(".json")) {
            return std::unexpected(fail("main_scene must be a project-relative path ending in .json", "/main_scene",
                                        "Use a '/' path without '..' such as scenes/main.scene.json"));
          }
        }
        if (args.contains("module")) {
          const std::string module = args["module"].get<std::string>();
          if (!module.empty() && !is_project_relative_path(module)) {
            return std::unexpected(fail("module must be empty or a project-relative path", "/module",
                                        "Use a '/' path without '..'"));
          }
        }
        ProjectFile& project = context.session().mutable_project();
        Json previous = Json::object();
        for (const char* key : {"name", "main_scene", "module"}) {
          if (!args.contains(key)) continue;
          std::string& field = std::string(key) == "name" ? project.name
                               : std::string(key) == "main_scene" ? project.main_scene
                                                                  : project.module;
          const std::string value = args[key].get<std::string>();
          if (field == value) continue;
          previous[key] = field;
          field = value;
        }
        const bool changed = !previous.empty();
        if (changed) context.mark_project_dirty();
        return CommandResult{{{"changed", changed}, {"previous", previous}, {"project", project_json(project)}},
                             changed ? std::optional<Invocation>(Invocation{"project.set", previous}) : std::nullopt,
                             std::nullopt};
      });
}

}  // namespace arcane::tooling
