#pragma once

#include <cstdint>
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
  // What redo should run instead of the call itself, when the effect is better stated another way
  // (tilemap edits: the exact tilemap.set_cells, which CommandHistory can merge within one stroke).
  std::optional<Invocation> redo;
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
  // inverse (reads, no-ops) are ignored. A non-empty `coalesce_key` merges the
  // call into the previous entry when that entry has the same key and no
  // end_gesture(), undo, redo or save point came in between (one drag = one
  // undo step): the original inverse is kept, the forward becomes the latest.
  //
  // `next_id_before` (Scene::next_id() before the call, 0 = unknown) makes undo rewind the id
  // counter, so a redo allocates the same node/component ids and later redo steps that name
  // them still resolve. Undo is strictly last-in-first-out, so no live id can be reused.
  void record(const Invocation& forward, const CommandResult& result, const std::string& coalesce_key = {},
              std::uint64_t next_id_before = 0);

  // Ends the current coalescing run (mouse released, edit committed).
  void end_gesture() { open_key_.clear(); }

  // Save-point tracking: the history position when the document was written.
  // at_save_point() is true again after undoing/redoing back to that position,
  // and never true once the saved state became unreachable.
  void mark_saved() {
    saved_ = undo_.size();
    open_key_.clear();
  }
  bool at_save_point() const noexcept { return saved_ == undo_.size(); }

  bool can_undo() const noexcept { return !undo_.empty(); }
  bool can_redo() const noexcept { return !redo_.empty(); }
  std::size_t undo_depth() const noexcept { return undo_.size(); }

  std::expected<void, ToolError> undo(const CommandRegistry& registry, ProjectContext& context);
  std::expected<void, ToolError> redo(const CommandRegistry& registry, ProjectContext& context);

 private:
  struct Entry {
    Invocation forward;
    Invocation inverse;
    std::uint64_t next_id_before = 0;
  };
  std::deque<Entry> undo_;
  std::deque<Entry> redo_;
  std::string open_key_;
  static constexpr std::size_t kUnreachable = static_cast<std::size_t>(-1);
  std::size_t saved_ = 0;  // undo depth at the save point, or kUnreachable once overwritten
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

// Registers tilemap.info/get/add_layer/set_cells/paint/erase/fill_rect/terrain_paint/dump_csv/import_csv (cells of a Tilemap in the scene)
// and tileset.info/validate/create/set_tile/set_rule/remove_rule (tileset files under assets/, written at once).
void register_tilemap_commands(CommandRegistry& registry);

// Registers hud.list/add/remove/set/move (the HUD layout in project.json, edited in memory; saved with the project).
void register_hud_commands(CommandRegistry& registry);

// Registers input.list/add_action/remove_action/restore_action/set_player/add_binding/remove_binding/set_binding
// (the input action map in project.json, edited in memory; saved with the project).
void register_input_commands(CommandRegistry& registry);

// Registers clip.slice/info/create/add_frames/remove_frame/move_frame/set_frame/set/restore (animation clip files under the
// project, written at once; clip.restore is the inverse of the others). Sheet sizes are read from PNG files.
void register_clip_commands(CommandRegistry& registry);

// Registers animgraph.create/info/add_param/remove_param/add_state/set_state/remove_state/add_event/remove_event/add_transition/
// remove_transition/restore (animation state machine files, written at once; animgraph.restore is the inverse of the others).
void register_animgraph_commands(CommandRegistry& registry);

// Registers project.get/project.set (name, main_scene, module in project.json, edited in memory; saved with the project).
void register_project_commands(CommandRegistry& registry);

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
