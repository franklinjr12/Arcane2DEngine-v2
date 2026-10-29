// transition.* / save.* commands (docs/formats/project.md, docs/formats/save.md,
// docs/commands.md). The config commands read and write project.json and the
// save commands read and write save files; neither touches the open scene, so
// they ignore `persist`/`dry_run` like the other asset-file commands.
#include <filesystem>

#include "arcane/canonical_json.hpp"
#include "arcane/project.hpp"
#include "arcane/save_game.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError args_error(std::string message, std::string path, std::string hint = {}) {
  return ToolError{"ARGS_INVALID", std::move(message), "", std::move(path), std::move(hint)};
}

constexpr const char* kProjectFile = "project.json";

std::expected<ProjectFile, ToolError> load_project_file(ProjectContext& context) {
  auto text = read_text_file(context.project_dir() / kProjectFile);
  if (!text) return std::unexpected(ToolError{"IO_ERROR", text.error(), kProjectFile, "", ""});
  auto project = load_project_from_json_strict(*text);
  if (!project) return std::unexpected(ToolError{"SCHEMA_INVALID", project.error(), kProjectFile, "", ""});
  return std::move(*project);
}

std::expected<void, ToolError> store_project_file(ProjectContext& context, const ProjectFile& project) {
  auto text = save_project_canonical_json(project);
  if (!text) return std::unexpected(ToolError{"VALUE_INVALID", text.error(), kProjectFile, "", ""});
  if (auto ok = write_text_file_atomic(context.project_dir() / kProjectFile, *text); !ok) {
    return std::unexpected(ToolError{"IO_ERROR", ok.error(), kProjectFile, "", ""});
  }
  return {};
}

Json transition_json(const TransitionConfig& t) {
  return {{"fade_out_seconds", t.fade_out_seconds},
          {"fade_in_seconds", t.fade_in_seconds},
          {"color", Json::array({t.color.r, t.color.g, t.color.b, t.color.a})}};
}

Json save_config_json(const SaveConfig& s) {
  const auto dir = save_directory(s.game_id);
  return {{"game_id", s.game_id}, {"data_version", s.data_version}, {"directory", dir.empty() ? "" : dir.string()}};
}

// Save folder for a command: explicit `dir`, else the per-user folder of the project's save.game_id.
std::expected<std::filesystem::path, ToolError> resolve_save_dir(ProjectContext& context, const Json& args) {
  if (args.contains("dir")) return std::filesystem::path(args["dir"].get<std::string>());
  auto project = load_project_file(context);
  if (!project) return std::unexpected(project.error());
  const auto dir = save_directory(project->save.game_id);
  if (dir.empty()) {
    return std::unexpected(ToolError{"VALUE_INVALID", "no save folder: the project has no save.game_id", kProjectFile,
                                     "/save/game_id", "Run save.config_set with game_id, or pass dir"});
  }
  return dir;
}

std::expected<SaveData, ToolError> read_slot(const std::filesystem::path& dir, const std::string& slot) {
  if (!is_valid_save_slot(slot)) {
    return std::unexpected(args_error("invalid slot name '" + slot + "'", "/slot", "Use 1-64 of A-Z a-z 0-9 _ -"));
  }
  auto data = read_save_game_file(dir, slot);
  if (!data) {
    const bool missing = !std::filesystem::exists(save_file_path(dir, slot));
    return std::unexpected(ToolError{missing ? "IO_ERROR" : "SCHEMA_INVALID", data.error(), slot, "",
                                     missing ? "List slots with save.list" : ""});
  }
  return std::move(*data);
}

std::expected<void, ToolError> write_slot(const std::filesystem::path& dir, const std::string& slot,
                                          const SaveData& data) {
  if (auto ok = write_save_game(dir, slot, data); !ok) return std::unexpected(ToolError{"IO_ERROR", ok.error(), slot, "", ""});
  return {};
}

Json value_json(const SaveValue& v) {
  if (const auto* b = std::get_if<bool>(&v)) return *b;
  if (const auto* i = std::get_if<int>(&v)) return *i;
  if (const auto* f = std::get_if<float>(&v)) return *f;
  return std::get<std::string>(v);
}

std::expected<SaveValue, ToolError> value_from_json(const Json& v, const std::string& path) {
  if (v.is_boolean()) return SaveValue(v.get<bool>());
  if (v.is_number_integer()) {
    const auto n = v.get<long long>();
    if (n < INT32_MIN || n > INT32_MAX) return std::unexpected(args_error("integer out of 32-bit range", path));
    return SaveValue(static_cast<int>(n));
  }
  if (v.is_number_float()) return SaveValue(v.get<float>());
  if (v.is_string()) return SaveValue(v.get<std::string>());
  return std::unexpected(args_error("value must be a boolean, number or string", path));
}

Json values_json(const SaveData& data) {
  Json values = Json::object();
  for (const auto& [key, value] : data.values) values[key] = value_json(value);
  return values;
}

const Json kDirProperty = {{"type", "string"}};

}  // namespace

void register_flow_commands(CommandRegistry& registry) {
  registry.add({"transition.get",
                "Read the authored scene-transition settings (fade seconds and colour) from project.json.",
                {{"type", "object"}, {"additionalProperties", false}},
                {{"type", "object"}},
                false},
               [](ProjectContext& context, const Json&) -> std::expected<CommandResult, ToolError> {
                 auto project = load_project_file(context);
                 if (!project) return std::unexpected(project.error());
                 return CommandResult{transition_json(project->transition), std::nullopt};
               });

  registry.add(
      {"transition.set",
       "Change the authored scene-transition settings in project.json; omitted fields keep their value. Writes at once.",
       {{"type", "object"},
        {"properties",
         {{"fade_out_seconds", {{"type", "number"}, {"minimum", 0}, {"maximum", 60}}},
          {"fade_in_seconds", {{"type", "number"}, {"minimum", 0}, {"maximum", 60}}},
          {"color", {{"type", "array"}}}}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto project = load_project_file(context);
        if (!project) return std::unexpected(project.error());
        const Json previous = transition_json(project->transition);
        TransitionConfig& t = project->transition;
        if (args.contains("fade_out_seconds")) t.fade_out_seconds = args["fade_out_seconds"].get<float>();
        if (args.contains("fade_in_seconds")) t.fade_in_seconds = args["fade_in_seconds"].get<float>();
        if (args.contains("color")) {
          const Json& c = args["color"];
          if (c.size() != 4) return std::unexpected(args_error("color must be [r, g, b, a]", "/color"));
          for (const Json& channel : c) {
            if (!channel.is_number()) return std::unexpected(args_error("color channels must be numbers", "/color"));
          }
          t.color = Color{c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        const Json now = transition_json(t);
        if (now != previous) {
          if (auto ok = store_project_file(context, *project); !ok) return std::unexpected(ok.error());
        }
        return CommandResult{{{"changed", now != previous}, {"previous", previous}, {"transition", now}},
                             now != previous ? std::optional<Invocation>(Invocation{"transition.set", previous})
                                             : std::nullopt};
      });

  registry.add({"save.config_get",
                "Read the authored save settings (game id, current data version, per-user save folder) from project.json.",
                {{"type", "object"}, {"additionalProperties", false}},
                {{"type", "object"}},
                false},
               [](ProjectContext& context, const Json&) -> std::expected<CommandResult, ToolError> {
                 auto project = load_project_file(context);
                 if (!project) return std::unexpected(project.error());
                 return CommandResult{save_config_json(project->save), std::nullopt};
               });

  registry.add(
      {"save.config_set",
       "Change the authored save settings in project.json (game_id: A-Z a-z 0-9 . _ -, empty clears; data_version >= 1). "
       "Writes at once.",
       {{"type", "object"},
        {"properties", {{"game_id", {{"type", "string"}}}, {"data_version", {{"type", "integer"}, {"minimum", 1}}}}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto project = load_project_file(context);
        if (!project) return std::unexpected(project.error());
        const SaveConfig before = project->save;
        if (args.contains("game_id")) {
          const std::string id = args["game_id"];
          if (!id.empty() && !is_valid_game_id(id)) {
            return std::unexpected(args_error("invalid game_id '" + id + "'", "/game_id", "Use 1-64 of A-Z a-z 0-9 . _ -"));
          }
          project->save.game_id = id;
        }
        if (args.contains("data_version")) project->save.data_version = args["data_version"];
        const bool changed = before.game_id != project->save.game_id || before.data_version != project->save.data_version;
        if (changed) {
          if (auto ok = store_project_file(context, *project); !ok) return std::unexpected(ok.error());
        }
        return CommandResult{{{"changed", changed}, {"previous", save_config_json(before)}, {"save", save_config_json(project->save)}},
                             changed ? std::optional<Invocation>(Invocation{
                                           "save.config_set", {{"game_id", before.game_id}, {"data_version", before.data_version}}})
                                     : std::nullopt};
      });

  registry.add({"save.list",
                "List the save slots in the project's per-user save folder (or `dir`) with their data versions.",
                {{"type", "object"}, {"properties", {{"dir", kDirProperty}}}, {"additionalProperties", false}},
                {{"type", "object"}},
                false},
               [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
                 auto dir = resolve_save_dir(context, args);
                 if (!dir) return std::unexpected(dir.error());
                 Json slots = Json::array();
                 for (const auto& slot : list_save_slots(*dir)) {
                   auto data = read_save_game_file(*dir, slot);
                   slots.push_back({{"slot", slot},
                                    {"data_version", data ? Json(data->data_version) : Json(nullptr)},
                                    {"valid", data.has_value()}});
                 }
                 return CommandResult{{{"directory", dir->string()}, {"slots", slots}}, std::nullopt};
               });

  registry.add({"save.get",
                "Read one save slot: its data version and values; with `key`, only that value.",
                {{"type", "object"},
                 {"properties", {{"slot", {{"type", "string"}}}, {"key", {{"type", "string"}}}, {"dir", kDirProperty}}},
                 {"required", {"slot"}},
                 {"additionalProperties", false}},
                {{"type", "object"}},
                false},
               [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
                 auto dir = resolve_save_dir(context, args);
                 if (!dir) return std::unexpected(dir.error());
                 auto data = read_slot(*dir, args["slot"]);
                 if (!data) return std::unexpected(data.error());
                 if (args.contains("key")) {
                   const std::string key = args["key"];
                   const SaveValue* value = data->find(key);
                   if (!value) {
                     return std::unexpected(ToolError{"VALUE_INVALID", "no key '" + key + "' in slot " + std::string(args["slot"]),
                                                      args["slot"], "/key", "List keys with save.get without key"});
                   }
                   return CommandResult{{{"key", key}, {"value", value_json(*value)}, {"data_version", data->data_version}},
                                        std::nullopt};
                 }
                 return CommandResult{{{"data_version", data->data_version}, {"values", values_json(*data)}}, std::nullopt};
               });

  registry.add(
      {"save.set",
       "Set one value (boolean, 32-bit integer, number or string) in a save slot, creating the slot if `create` is true. "
       "Writes atomically at once; the data version is kept.",
       {{"type", "object"},
        {"properties",
         {{"slot", {{"type", "string"}}},
          {"key", {{"type", "string"}}},
          {"value", Json::object()},
          {"create", {{"type", "boolean"}}},
          {"dir", kDirProperty}}},
        {"required", {"slot", "key", "value"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto dir = resolve_save_dir(context, args);
        if (!dir) return std::unexpected(dir.error());
        const std::string slot = args["slot"];
        const std::string key = args["key"];
        if (!is_valid_save_key(key)) return std::unexpected(args_error("invalid key '" + key + "'", "/key", "Use 1-128 of A-Z a-z 0-9 . _ - : /"));
        auto value = value_from_json(args["value"], "/value");
        if (!value) return std::unexpected(value.error());
        SaveData data;
        if (is_valid_save_slot(slot) && !std::filesystem::exists(save_file_path(*dir, slot)) && args.value("create", false)) {
          auto project = load_project_file(context);
          if (!project) return std::unexpected(project.error());
          data.data_version = project->save.data_version;
        } else {
          auto existing = read_slot(*dir, slot);
          if (!existing) return std::unexpected(existing.error());
          data = std::move(*existing);
        }
        const SaveValue* old = data.find(key);
        const Json previous = old ? value_json(*old) : Json(nullptr);
        data.set(key, *value);
        if (auto ok = write_slot(*dir, slot, data); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"slot", slot}, {"key", key}, {"value", value_json(*value)}, {"previous", previous}}, std::nullopt};
      });

  registry.add(
      {"save.remove",
       "Remove one key from a save slot. Writes atomically at once; fails if the key does not exist.",
       {{"type", "object"},
        {"properties", {{"slot", {{"type", "string"}}}, {"key", {{"type", "string"}}}, {"dir", kDirProperty}}},
        {"required", {"slot", "key"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto dir = resolve_save_dir(context, args);
        if (!dir) return std::unexpected(dir.error());
        const std::string slot = args["slot"];
        const std::string key = args["key"];
        auto data = read_slot(*dir, slot);
        if (!data) return std::unexpected(data.error());
        if (!data->values.erase(key)) {
          return std::unexpected(ToolError{"VALUE_INVALID", "no key '" + key + "' in slot " + slot, slot, "/key", ""});
        }
        if (auto ok = write_slot(*dir, slot, *data); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"slot", slot}, {"removed", key}}, std::nullopt};
      });

  registry.add(
      {"save.migrate",
       "Upgrade a save slot to `to` (default: the project's save.data_version) in one declarative step: `rename` "
       "{old:new}, `remove` [keys], `defaults` {key:value} (only for keys still missing), applied in that order, then "
       "data_version is set. Writes atomically; any failure writes nothing. Games with code migrations use SaveMigration.",
       {{"type", "object"},
        {"properties",
         {{"slot", {{"type", "string"}}},
          {"to", {{"type", "integer"}, {"minimum", 1}}},
          {"rename", {{"type", "object"}}},
          {"remove", {{"type", "array"}}},
          {"defaults", {{"type", "object"}}},
          {"dir", kDirProperty}}},
        {"required", {"slot"}},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto dir = resolve_save_dir(context, args);
        if (!dir) return std::unexpected(dir.error());
        const std::string slot = args["slot"];
        auto data = read_slot(*dir, slot);
        if (!data) return std::unexpected(data.error());
        int to = 0;
        if (args.contains("to")) {
          to = args["to"];
        } else {
          auto project = load_project_file(context);
          if (!project) return std::unexpected(project.error());
          to = project->save.data_version;
        }
        const int from = data->data_version;
        if (to < from) {
          return std::unexpected(ToolError{"VALUE_INVALID", "slot is at data_version " + std::to_string(from) + ", newer than " + std::to_string(to),
                                           slot, "/to", "Saves are never downgraded"});
        }
        if (to == from && (args.contains("rename") || args.contains("remove") || args.contains("defaults"))) {
          return std::unexpected(args_error("the slot is already at data_version " + std::to_string(to) + "; steps would not be a migration", "/to"));
        }
        const Json renames = args.value("rename", Json::object());
        for (const auto& [from_key, to_key] : renames.items()) {
          if (!to_key.is_string() || !is_valid_save_key(to_key.get<std::string>())) {
            return std::unexpected(args_error("rename target of '" + from_key + "' is not a valid key", "/rename/" + from_key));
          }
          auto it = data->values.find(from_key);
          if (it == data->values.end()) {
            return std::unexpected(ToolError{"VALUE_INVALID", "rename: no key '" + from_key + "' in slot " + slot, slot, "/rename/" + from_key, ""});
          }
          SaveValue moved = std::move(it->second);
          data->values.erase(it);
          data->values[to_key.get<std::string>()] = std::move(moved);
        }
        const Json removals = args.value("remove", Json::array());
        for (const Json& key : removals) {
          if (!key.is_string()) return std::unexpected(args_error("remove entries must be strings", "/remove"));
          data->values.erase(key.get<std::string>());
        }
        const Json defaults = args.value("defaults", Json::object());
        for (const auto& [key, v] : defaults.items()) {
          if (!is_valid_save_key(key)) return std::unexpected(args_error("invalid key '" + key + "'", "/defaults/" + key));
          auto value = value_from_json(v, "/defaults/" + key);
          if (!value) return std::unexpected(value.error());
          data->values.emplace(key, *value);
        }
        data->data_version = to;
        if (auto ok = write_slot(*dir, slot, *data); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"slot", slot}, {"from_version", from}, {"data_version", to}, {"values", values_json(*data)}}, std::nullopt};
      });
}

}  // namespace arcane::tooling
