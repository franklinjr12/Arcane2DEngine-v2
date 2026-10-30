// Helpers shared by the offline command files (offline.cpp, asset_commands.cpp); not a public header.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "arcane_tooling/offline.hpp"

namespace arcane::tooling {

ToolError offline_error(const char* code, std::string message, std::string file = {}, std::string path = {}, std::string hint = {});
Json offline_object_schema(Json properties, std::initializer_list<const char*> required = {},
                           std::initializer_list<const char*> positional = {});
std::expected<void, ToolError> offline_require_project(const OfflineEnv& env);

// asset.list / asset.import / asset.reimport (asset_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_asset_commands();

// run / test (run_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_run_commands();

// export (export_commands.cpp); appended by make_commands().
std::vector<OfflineCommand> make_export_commands();

}  // namespace arcane::tooling
