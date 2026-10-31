// Helpers shared by the offline command files (offline.cpp, asset_commands.cpp); not a public header.
#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "arcane/hud.hpp"
#include "arcane/replay.hpp"
#include "arcane_tooling/context.hpp"
#include "arcane_tooling/offline.hpp"

namespace arcane::tooling {

ToolError offline_error(const char* code, std::string message, std::string file = {}, std::string path = {}, std::string hint = {});
Json offline_object_schema(Json properties, std::initializer_list<const char*> required = {},
                           std::initializer_list<const char*> positional = {});
std::expected<void, ToolError> offline_require_project(const OfflineEnv& env);

// Error ARGS_INVALID unless `value` is a project-relative '/' path.
std::expected<void, ToolError> offline_check_path(const std::string& value, const char* arg);

// Fixed-frame run on a fresh copy of the context scene (run_commands.cpp), shared by run, test and render.
struct RunOutput {
  std::vector<std::uint64_t> hashes;
  std::size_t nodes = 0;
  bool module = false;
};
using RunFinish = std::function<std::expected<void, ToolError>(Scene&, Hud&)>;
std::expected<RunOutput, ToolError> run_on_copy(ProjectContext& context, FixedRunOptions options, const std::string& file,
                                                const RunFinish& finish = {});

// render (render_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_render_commands();

// asset.list / asset.import / asset.reimport (asset_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_asset_commands();

// run / test (run_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_run_commands();

// export (export_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_export_commands();

}  // namespace arcane::tooling
