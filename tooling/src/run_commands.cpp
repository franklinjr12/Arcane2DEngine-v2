// run / test: display-free fixed-frame runs of the project (docs/commands.md). Each opens its own
// ProjectContext and steps a fresh copy of the scene, so the context scene is never advanced or saved.
#include <algorithm>
#include <filesystem>
#include <optional>

#include "arcane/atlas.hpp"
#include "arcane/audio.hpp"
#include "arcane/canonical_json.hpp"
#include "arcane/game_context.hpp"
#include "arcane/hud.hpp"
#include "arcane/project.hpp"
#include "arcane/replay.hpp"
#include "arcane_tooling/context.hpp"
#include "arcane_tooling/offline.hpp"
#include "offline_internal.hpp"

namespace arcane::tooling {

namespace {

using Result = std::expected<Json, ToolError>;

std::size_t count_nodes(const Node& node) {
  std::size_t n = 1;
  for (const auto& child : node.children()) n += count_nodes(*child);
  return n;
}

}  // namespace

std::expected<void, ToolError> offline_check_path(const std::string& value, const char* arg) {
  if (is_project_relative_path(value)) return {};
  return std::unexpected(offline_error("ARGS_INVALID", "'" + value + "' is not a project-relative '/' path", "",
                                       std::string("/") + arg));
}

// Runs `options` on a fresh copy of the context scene with the module's on_update before each step,
// like arcane_player --headless. Module exceptions become MODULE_FAILURE. `finish`, if set, runs on the stepped
// scene while the audio and HUD globals are still active (used by render).
std::expected<RunOutput, ToolError> run_on_copy(ProjectContext& context, FixedRunOptions options, const std::string& file,
                                                const RunFinish& finish) {
  auto scene_text = context.scene().save_canonical_json(context.registry());
  if (!scene_text) return std::unexpected(offline_error("VALUE_INVALID", scene_text.error(), context.scene_file()));
  auto scene = Scene::load_from_json(*scene_text, context.registry());
  if (!scene) return std::unexpected(offline_error("SCHEMA_INVALID", scene.error(), context.scene_file()));

  const GameApi* api = context.module_api();
  if (api) options.before_step = [api](float dt) { api->on_update(dt); };
  InputActionMap actions = context.session().project().input_actions;
  Hud hud = context.session().project().hud;
  auto audio = AudioSystem::create(true);  // null backend: no device
  if (!audio) return std::unexpected(offline_error("IO_ERROR", audio.error()));
  GameContext game_context(context.project_dir(), context.registry());
  game_context.set_collision_layers(context.session().project().collision_layers);
  const DisplayConfig& display = context.session().project().display;
  if (options.viewport_width <= 0 || options.viewport_height <= 0) {  // no explicit size: the project's design resolution
    options.viewport_width = display.design_width;
    options.viewport_height = display.design_height;
  }
  if (options.viewport_width > 0 && options.viewport_height > 0) game_context.set_viewport_size(options.viewport_width, options.viewport_height);
  options.restart_scene_json = *scene_text;
  set_active_audio_system(&*audio);
  set_active_game_context(&game_context);
  set_active_hud(&hud);
  struct Reset {
    ~Reset() {
      set_active_game_context(nullptr);
      set_active_audio_system(nullptr);
      set_active_hud(nullptr);
    }
  } reset;
  try {
    // frames == 0 (render only): no stepping, the trace is just the initial state hash.
    auto hashes = options.frames == 0 && !options.recording
                      ? std::expected<std::vector<std::uint64_t>, std::string>(std::vector{hash_scene_state(*scene, context.registry())})
                      : run_fixed_frames(*scene, context.registry(), actions, options);
    if (!hashes) return std::unexpected(offline_error("VALUE_INVALID", hashes.error(), file));
    std::size_t nodes = 0;
    for (const auto& root : scene->roots()) nodes += count_nodes(*root);
    if (finish) {
      if (auto done = finish(*scene, hud); !done) return std::unexpected(done.error());
    }
    return RunOutput{std::move(*hashes), nodes, api != nullptr};
  } catch (const std::exception& ex) {
    return std::unexpected(offline_error("MODULE_FAILURE", ex.what()));
  }
}

namespace {

std::expected<ReplayRecording, ToolError> load_recording(const std::filesystem::path& dir, const std::string& file) {
  auto text = read_text_file(dir / file);
  if (!text) return std::unexpected(offline_error("IO_ERROR", text.error(), file));
  auto recording = load_recording_json(*text);
  if (!recording) return std::unexpected(offline_error("SCHEMA_INVALID", recording.error(), file));
  return std::move(*recording);
}

Result run(const OfflineEnv& env, const Json& args, bool dry_run) {
  const bool has_replay = args.contains("replay");
  if (has_replay && (args.contains("frames") || args.contains("fixed_hz"))) {
    return std::unexpected(offline_error("ARGS_INVALID", "replay supplies frames and fixed_hz; do not pass them as well", "", "/replay"));
  }
  for (const char* key : {"replay", "trace_out"}) {
    if (args.contains(key)) {
      if (auto ok = offline_check_path(args[key].get<std::string>(), key); !ok) return std::unexpected(ok.error());
    }
  }
  auto context = ProjectContext::open(env.project_dir, env.module_override, env.scene_override);
  if (!context) return std::unexpected(context.error());

  FixedRunOptions options;
  std::optional<ReplayRecording> recording;
  std::string file;
  if (has_replay) {
    file = args["replay"].get<std::string>();
    auto loaded = load_recording(env.project_dir, file);
    if (!loaded) return std::unexpected(loaded.error());
    recording = std::move(*loaded);
    options.recording = &*recording;
  } else {
    options.frames = args.value("frames", 60);
    options.fixed_hz = args.value("fixed_hz", 60);
    options.seed = args.value("seed", 0);
  }
  const int fixed_hz = recording ? recording->fixed_hz : options.fixed_hz;
  auto out = run_on_copy(**context, options, file);
  if (!out) return std::unexpected(out.error());

  Json trace_out = nullptr;
  if (args.contains("trace_out")) {
    const std::string rel = args["trace_out"].get<std::string>();
    trace_out = rel;
    if (!dry_run) {
      auto text = save_trace_json(out->hashes);
      if (!text) return std::unexpected(offline_error("VALUE_INVALID", text.error()));
      const auto target = env.project_dir / rel;
      std::error_code ec;
      std::filesystem::create_directories(target.parent_path(), ec);
      if (auto ok = write_text_file_atomic(target, *text); !ok) return std::unexpected(offline_error("IO_ERROR", ok.error(), rel));
    }
  }
  return Json{{"frames", out->hashes.size()}, {"fixed_hz", fixed_hz}, {"final_hash", hash_to_hex(out->hashes.back())},
              {"nodes", out->nodes}, {"module", out->module}, {"trace_out", trace_out}};
}

Result test(const OfflineEnv& env, const Json& args, bool) {
  auto context = ProjectContext::open(env.project_dir, env.module_override, env.scene_override);
  if (!context) return std::unexpected(context.error());
  const std::string filter = args.value("filter", "");

  std::vector<std::string> names;
  std::error_code ec;
  const auto tests_dir = env.project_dir / "tests";
  if (std::filesystem::is_directory(tests_dir, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(tests_dir, ec)) {
      const std::string name = entry.path().filename().string();
      constexpr std::string_view suffix = ".replay.json";
      if (!entry.is_regular_file() || name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
      const std::string stem = name.substr(0, name.size() - suffix.size());
      if (filter.empty() || stem.find(filter) != std::string::npos) names.push_back(stem);
    }
  }
  std::sort(names.begin(), names.end());

  Json cases = Json::array();
  int passed = 0, failed = 0;
  for (const auto& name : names) {
    const std::string rec_file = "tests/" + name + ".replay.json", trace_file = "tests/" + name + ".trace.json";
    Json entry = {{"name", name}, {"recording", rec_file}, {"expected", trace_file}, {"frames", 0}, {"passed", false},
                  {"first_divergent_frame", nullptr}, {"error", nullptr}};
    [&] {
      auto recording = load_recording(env.project_dir, rec_file);
      if (!recording) { entry["error"] = recording.error().message; return; }
      entry["frames"] = recording->frames;
      if (!std::filesystem::is_regular_file(env.project_dir / trace_file, ec)) {
        entry["error"] = "missing expected trace; create it with `arcane run --replay ... --trace_out ...`";
        return;
      }
      auto text = read_text_file(env.project_dir / trace_file);
      auto expected = text ? load_trace_json(*text) : std::unexpected(text.error());
      if (!expected) { entry["error"] = expected.error(); return; }
      FixedRunOptions options;
      options.recording = &*recording;
      auto out = run_on_copy(**context, options, rec_file);
      if (!out) { entry["error"] = out.error().message; return; }
      if (auto frame = first_divergent_frame(*expected, out->hashes)) {
        entry["first_divergent_frame"] = *frame;
        entry["error"] = "first divergent frame is " + std::to_string(*frame);
        return;
      }
      entry["passed"] = true;
    }();
    (entry["passed"] == true ? passed : failed)++;
    cases.push_back(std::move(entry));
  }

  FixedRunOptions smoke;
  smoke.frames = args.value("frames", 60);
  auto out = run_on_copy(**context, smoke, "");
  if (!out) return std::unexpected(out.error());
  return Json{{"ok", failed == 0}, {"passed", passed}, {"failed", failed}, {"cases", cases},
              {"smoke", {{"frames", out->hashes.size()}, {"final_hash", hash_to_hex(out->hashes.back())}}}};
}

}  // namespace

std::vector<OfflineCommand> make_run_commands() {
  const Json kStr = {{"type", "string"}};
  std::vector<OfflineCommand> c;
  c.push_back({{"run",
                "Display-free fixed-frame run of the main (or --scene) scene, calling the game module's on_update each step; no window, GL or audio device. "
                "With replay, frames/fixed_hz/seed come from the recording. trace_out writes an arcane.replay_trace (skipped by --dry-run).",
                offline_object_schema({{"frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}},
                                       {"fixed_hz", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
                                       {"seed", {{"type", "integer"}, {"minimum", 0}}},
                                       {"replay", kStr},
                                       {"trace_out", kStr}}),
                {{"type", "object"}},
                true},
               run});
  c.push_back({{"test",
                "Run the project's replay tests: every tests/NAME.replay.json is replayed with the module and compared to tests/NAME.trace.json, plus a smoke run; exit 4 if any case fails.",
                offline_object_schema({{"filter", kStr}, {"frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000000}}}}),
                {{"type", "object"}},
                false},
               test});
  return c;
}

}  // namespace arcane::tooling
