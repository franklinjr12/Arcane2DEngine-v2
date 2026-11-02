#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

// Commands that work on files rather than on an open scene: project and scene
// files, validation, formatting, migration and schemas. They are described by
// the same CommandSpec as registry commands (so docs and argument checking are
// shared) but run without a ProjectContext, because creating a project or
// migrating one must work when the project cannot be opened yet.
struct OfflineEnv {
  std::filesystem::path project_dir;  // --project, default "."
  std::string module_override;        // --module
  std::string scene_override;         // --scene
  const CommandRegistry* registry = nullptr;
};

struct OfflineCommand {
  CommandSpec spec;  // `mutating` = writes files
  // Returns the JSON result; `dry_run` computes it without writing. A result with "ok": false is a
  // reported validation failure (CLI exit 4) rather than an error.
  std::function<std::expected<Json, ToolError>(const OfflineEnv&, const Json& args, bool dry_run)> run;
};

const std::vector<OfflineCommand>& offline_commands();
const OfflineCommand* find_offline_command(const std::string& name);

// The static JSON schemas from schemas/, embedded at build time. `name` is e.g. "arcane.scene".
struct EmbeddedSchema {
  std::string_view name;
  std::string_view text;
};
const std::vector<EmbeddedSchema>& embedded_schemas();

// Markdown reference of every registry and offline command (checked in as docs/cli-reference.md).
std::string generate_cli_reference(const CommandRegistry& registry);

// The `arcane` executable: `args` excludes argv[0]. Result JSON goes to `out`, diagnostics to `err`;
// returns the process exit status.
int run_cli(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace arcane::tooling
