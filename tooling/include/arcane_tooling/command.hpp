#pragma once

#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "arcane_tooling/context.hpp"
#include "arcane_tooling/error.hpp"

namespace arcane::tooling {

// A command call: registered name plus JSON arguments.
struct Invocation {
  std::string command;
  Json args = Json::object();
};

// What a command returns: the JSON result for the caller and, when it
// changed something, the invocation that restores the previous state.
// `inverse` is empty for reads and for no-op (idempotent) calls.
struct CommandResult {
  Json value = Json::object();
  std::optional<Invocation> inverse;
};

using CommandFn = std::function<std::expected<CommandResult, ToolError>(ProjectContext&, const Json& args)>;

// Static description of a command. `args_schema` is a small JSON-Schema
// subset (type, properties, required, additionalProperties, minimum,
// maximum, enum); a property without "type" accepts any JSON value (typed
// by the command itself, e.g. a property value).
struct CommandSpec {
  std::string name;  // dotted, e.g. "prop.set"
  std::string summary;
  Json args_schema;
  Json result_schema;
  bool mutating = false;
  // Writes files outside the open scene as it runs (atlas, save, project.json ...), ignoring
  // persist/dry_run; the CLI refuses --dry-run for these. Set with mark_immediate_write.
  bool immediate_write = false;
};

struct ExecuteOptions {
  bool persist = false;  // mutating commands: save to disk; roll back in memory if saving fails
  bool dry_run = false;  // validate and run, then restore the previous state; nothing is saved
};

// Name -> command table shared by the CLI, batch runner and (later) the
// editor. No ImGui or window dependencies.
class CommandRegistry {
 public:
  void add(CommandSpec spec, CommandFn fn);

  const CommandSpec* find(const std::string& name) const;
  void mark_immediate_write(const std::string& name);
  std::vector<const CommandSpec*> list() const;  // sorted by name

  // Validates `args` against the schema, runs the command and applies
  // `options`. On any error the in-memory project is as before the call.
  std::expected<CommandResult, ToolError> execute(ProjectContext& context, const Invocation& call,
                                                  const ExecuteOptions& options = {}) const;

 private:
  struct Entry {
    CommandSpec spec;
    CommandFn fn;
  };
  std::map<std::string, Entry> entries_;
};

// Checks `value` against the schema subset above; the error path is a
// JSON pointer into `value` (e.g. "/node").
std::expected<void, ToolError> validate_args(const Json& schema, const Json& value);

// Undo/redo for edits made through commands (the GUI's model; the CLI
// persists immediately and keeps no history). Entries hold the forward and
// inverse invocations, so undo/redo are ordinary command executions.
class CommandHistory {
 public:
  // Records a successful execution; clears the redo stack. Calls with no
  // inverse (reads, no-ops) are ignored.
  void record(const Invocation& forward, const CommandResult& result);

  bool can_undo() const noexcept { return !undo_.empty(); }
  bool can_redo() const noexcept { return !redo_.empty(); }
  std::size_t undo_depth() const noexcept { return undo_.size(); }

  std::expected<void, ToolError> undo(const CommandRegistry& registry, ProjectContext& context);
  std::expected<void, ToolError> redo(const CommandRegistry& registry, ProjectContext& context);

 private:
  struct Entry {
    Invocation forward;
    Invocation inverse;
  };
  std::deque<Entry> undo_;
  std::deque<Entry> redo_;
};

// Registers scene.tree/query/restore and node.info/find/add/remove/rename/reparent/duplicate/set_transform.
void register_scene_commands(CommandRegistry& registry);

// Registers component.types/list/add/remove.
void register_component_commands(CommandRegistry& registry);

// Registers every command above (used by the CLI and the tests) and marks the ones that write
// files immediately.
void register_all_commands(CommandRegistry& registry);

// Registers the property commands: prop.list, prop.get, prop.set.
void register_property_commands(CommandRegistry& registry);

// Registers prefab.save, instantiate, remove, override, revert, break_link, relink, info.
void register_prefab_commands(CommandRegistry& registry);

// Registers audio.backend (backend selection diagnostics; opens no device by default).
void register_audio_commands(CommandRegistry& registry);

// Registers atlas.pack, atlas.regions, atlas.make_clip (asset files only; the scene is untouched).
void register_atlas_commands(CommandRegistry& registry);

// Registers transition.get/set, save.config_get/config_set and save.list/get/set/remove/migrate
// (project.json settings and save files only; the scene is untouched).
void register_flow_commands(CommandRegistry& registry);

// Registers debug.get/debug.set (the overlay settings in project.json; writes at once).
void register_debug_commands(CommandRegistry& registry);

// Registers bench.run (headless benchmark; needs a project context only because every command takes one).
void register_bench_commands(CommandRegistry& registry);

// Registers replay.save (write an input recording) and replay.run (replay it headless, hash every frame,
// compare with an expected trace). The open scene is never advanced: replays run on a copy.
void register_replay_commands(CommandRegistry& registry);

// The headless benchmark behind bench.run and `arcane_bench`: validates `args` (sprites, emitters,
// particles_per_emitter, bodies, frames; all optional integers) and returns the stable
// `arcane.bench` v1 document {format, version, config, results}.
std::expected<Json, ToolError> run_bench_json(const Json& args);

// Node / component selectors shared by commands: "id:42" or
// "path:/Root/Player" (must resolve to exactly one node).
std::expected<Node*, ToolError> resolve_node(ProjectContext& context, const std::string& selector);

}  // namespace arcane::tooling
