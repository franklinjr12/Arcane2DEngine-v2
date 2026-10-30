// bench.run: the headless benchmark (docs/perf.md). Also used by the arcane_bench executable.
#include "arcane/debug.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

namespace {

Json count_property(int maximum) {
  return {{"type", "integer"}, {"minimum", 0}, {"maximum", maximum}};
}

const char* const kCountKeys[] = {"sprites", "emitters", "particles_per_emitter", "bodies", "frames"};

}  // namespace

std::expected<Json, ToolError> run_bench_json(const Json& args) {
  if (!args.is_object()) return std::unexpected(ToolError{"ARGS_INVALID", "arguments must be an object", "", "", ""});
  for (const auto& [key, value] : args.items()) {
    bool known = false;
    for (const char* k : kCountKeys) known = known || key == k;
    if (!known) return std::unexpected(ToolError{"ARGS_INVALID", "unknown argument '" + key + "'", "", "/" + key, ""});
    if (!value.is_number_integer()) {
      return std::unexpected(ToolError{"ARGS_INVALID", "'" + key + "' must be an integer", "", "/" + key, ""});
    }
  }
  BenchConfig config;
  if (args.contains("sprites")) config.sprites = args["sprites"].get<int>();
  if (args.contains("emitters")) config.emitters = args["emitters"].get<int>();
  if (args.contains("particles_per_emitter")) config.particles_per_emitter = args["particles_per_emitter"].get<int>();
  if (args.contains("bodies")) config.bodies = args["bodies"].get<int>();
  if (args.contains("frames")) config.frames = args["frames"].get<int>();
  auto result = run_benchmark(config);
  if (!result) {
    return std::unexpected(ToolError{"ARGS_INVALID", result.error(), "", "", "See docs/perf.md for the ranges"});
  }
  return Json{{"format", "arcane.bench"},
              {"version", 1},
              {"config",
               {{"sprites", config.sprites},
                {"emitters", config.emitters},
                {"particles_per_emitter", config.particles_per_emitter},
                {"bodies", config.bodies},
                {"frames", config.frames}}},
              {"results",
               {{"setup_ms", result->setup_ms},
                {"total_ms", result->total_ms},
                {"avg_frame_ms", result->avg_frame_ms},
                {"max_frame_ms", result->max_frame_ms},
                {"live_particles", result->live_particles},
                {"physics_bodies", result->physics_bodies}}}};
}

void register_bench_commands(CommandRegistry& registry) {
  registry.add({"bench.run",
                "Run the headless benchmark (no window, GL or audio): sprite nodes, particle emitters and falling "
                "bodies stepped for N fixed frames. Returns an arcane.bench v1 document; timings vary per machine.",
                {{"type", "object"},
                 {"properties",
                  {{"sprites", count_property(1000000)},
                   {"emitters", count_property(100000)},
                   {"particles_per_emitter", count_property(256)},
                   {"bodies", count_property(100000)},
                   {"frames", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100000}}}}},
                 {"additionalProperties", false}},
                {{"type", "object"}},
                false},
               [](ProjectContext&, const Json& args) -> std::expected<CommandResult, ToolError> {
                 auto doc = run_bench_json(args);
                 if (!doc) return std::unexpected(doc.error());
                 return CommandResult{*doc, std::nullopt};
               });
}

}  // namespace arcane::tooling
