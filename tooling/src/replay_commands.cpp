// replay.* commands (docs/formats/replay.md, docs/commands.md). Recordings and traces are asset files; the open
// scene is never advanced (replays run on a copy), so these ignore `persist`/`dry_run` like atlas.*.
#include <filesystem>

#include "arcane/atlas.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/project.hpp"
#include "arcane/replay.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

ToolError args_error(std::string message, std::string path) {
  return ToolError{"ARGS_INVALID", std::move(message), "", std::move(path), ""};
}

std::expected<void, ToolError> check_path(const std::string& value, const char* arg) {
  if (is_project_relative_path(value)) return {};
  return std::unexpected(args_error("'" + value + "' is not a project-relative '/' path", std::string("/") + arg));
}

std::expected<std::string, ToolError> read_asset(ProjectContext& context, const std::string& file) {
  auto text = read_text_file(context.project_dir() / file);
  if (!text) return std::unexpected(ToolError{"IO_ERROR", text.error(), file, "", ""});
  return std::move(*text);
}

std::expected<void, ToolError> write_asset(ProjectContext& context, const std::string& file, const std::string& text) {
  const auto target = context.project_dir() / file;
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (auto ok = write_text_file_atomic(target, text); !ok) {
    return std::unexpected(ToolError{"IO_ERROR", ok.error(), file, "", ""});
  }
  return {};
}

Json hashes_json(const std::vector<std::uint64_t>& hashes) {
  Json list = Json::array();
  for (std::uint64_t h : hashes) list.push_back(hash_to_hex(h));
  return list;
}

}  // namespace

void register_replay_commands(CommandRegistry& registry) {
  registry.add(
      {"replay.save",
       "Write an input recording (arcane.replay v1, docs/formats/replay.md): fixed rate, frame count, optional seed and "
       "the input changes per fixed frame. The events array is validated exactly like a recording file.",
       {{"type", "object"},
        {"properties",
         {{"file", {{"type", "string"}}},
          {"frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}},
          {"fixed_hz", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
          {"seed", {{"type", "integer"}, {"minimum", 0}}},
          {"events", {{"type", "array"}}}}},
        {"required", Json::array({"file", "frames"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string file = args["file"].get<std::string>();
        if (auto ok = check_path(file, "file"); !ok) return std::unexpected(ok.error());
        Json doc = {{"format", "arcane.replay"}, {"version", 1}, {"frames", args["frames"]},
                    {"events", args.value("events", Json::array())}};
        if (args.contains("fixed_hz")) doc["fixed_hz"] = args["fixed_hz"];
        if (args.contains("seed")) doc["seed"] = args["seed"];
        auto recording = load_recording_json(doc.dump());
        if (!recording) return std::unexpected(ToolError{"VALUE_INVALID", recording.error(), file, "", ""});
        auto text = save_recording_json(*recording);
        if (!text) return std::unexpected(ToolError{"VALUE_INVALID", text.error(), file, "", ""});
        if (auto ok = write_asset(context, file, *text); !ok) return std::unexpected(ok.error());
        return CommandResult{{{"file", file}, {"frames", recording->frames}, {"events", recording->events.size()}}, std::nullopt};
      });

  registry.add(
      {"replay.run",
       "Replay a recording headless on a copy of the open scene (fixed timestep, physics, the project's input actions) "
       "and hash the scene state after every frame. With `expected` (a trace file) fails with REPLAY_DIVERGED naming "
       "the first divergent frame; with `trace_out` writes the trace (arcane.replay_trace v1). Game-module "
       "on_update is not called. Same build and platform only.",
       {{"type", "object"},
        {"properties",
         {{"recording", {{"type", "string"}}}, {"expected", {{"type", "string"}}}, {"trace_out", {{"type", "string"}}}}},
        {"required", Json::array({"recording"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string rec_file = args["recording"].get<std::string>();
        if (auto ok = check_path(rec_file, "recording"); !ok) return std::unexpected(ok.error());
        for (const char* key : {"expected", "trace_out"}) {
          if (args.contains(key)) {
            if (auto ok = check_path(args[key].get<std::string>(), key); !ok) return std::unexpected(ok.error());
          }
        }
        auto rec_text = read_asset(context, rec_file);
        if (!rec_text) return std::unexpected(rec_text.error());
        auto recording = load_recording_json(*rec_text);
        if (!recording) return std::unexpected(ToolError{"SCHEMA_INVALID", recording.error(), rec_file, "", ""});

        std::vector<std::uint64_t> expected;
        if (args.contains("expected")) {
          const std::string file = args["expected"].get<std::string>();
          auto text = read_asset(context, file);
          if (!text) return std::unexpected(text.error());
          auto trace = load_trace_json(*text);
          if (!trace) return std::unexpected(ToolError{"SCHEMA_INVALID", trace.error(), file, "", ""});
          expected = std::move(*trace);
        }

        auto project_text = read_asset(context, "project.json");
        if (!project_text) return std::unexpected(project_text.error());
        auto project = load_project_from_json_strict(*project_text);
        if (!project) return std::unexpected(ToolError{"SCHEMA_INVALID", project.error(), "project.json", "", ""});
        auto scene_text = context.scene().save_canonical_json(context.registry());
        if (!scene_text) return std::unexpected(ToolError{"VALUE_INVALID", scene_text.error(), context.scene_file(), "", ""});
        auto scene = Scene::load_from_json(*scene_text, context.registry());
        if (!scene) return std::unexpected(ToolError{"SCHEMA_INVALID", scene.error(), context.scene_file(), "", ""});

        std::vector<std::uint64_t> hashes;
        try {
          auto result = run_replay(*scene, context.registry(), project->input_actions, *recording);
          if (!result) return std::unexpected(ToolError{"VALUE_INVALID", result.error(), rec_file, "", ""});
          hashes = std::move(*result);
        } catch (const std::exception& ex) {
          return std::unexpected(ToolError{"MODULE_FAILURE", ex.what(), "", "", ""});
        }

        if (args.contains("trace_out")) {
          auto text = save_trace_json(hashes);
          if (!text) return std::unexpected(ToolError{"VALUE_INVALID", text.error(), "", "", ""});
          if (auto ok = write_asset(context, args["trace_out"].get<std::string>(), *text); !ok) return std::unexpected(ok.error());
        }
        if (args.contains("expected")) {
          if (auto frame = first_divergent_frame(expected, hashes)) {
            return std::unexpected(ToolError{"REPLAY_DIVERGED", "first divergent frame is " + std::to_string(*frame),
                                             args["expected"].get<std::string>(), "/hashes/" + std::to_string(*frame),
                                             "The scene, input or build changed since the trace was written"});
          }
        }
        return CommandResult{{{"frames", hashes.size()},
                              {"final_hash", hash_to_hex(hashes.back())},
                              {"hashes", hashes_json(hashes)},
                              {"matches_expected", args.contains("expected")}},
                             std::nullopt};
      });
}

}  // namespace arcane::tooling
