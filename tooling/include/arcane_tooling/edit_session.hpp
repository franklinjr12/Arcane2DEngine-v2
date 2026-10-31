#pragma once

#include <expected>
#include <string>

#include "arcane/property.hpp"
#include "arcane_tooling/command.hpp"

namespace arcane::tooling {

// The editor's way to change authored scene data: every edit is a registry
// command (the same ones as the `arcane` CLI), recorded in a CommandHistory
// for undo/redo, drag coalescing and save-point dirty tracking. No ImGui.
// Node/Component pointers taken before run()/undo()/redo() may be invalid
// afterwards (structural undo restores the scene); re-find them by id.
class EditSession {
 public:
  EditSession(ProjectContext& context, const CommandRegistry& registry);

  // Executes and records. A non-empty `coalesce_key` merges consecutive calls
  // with the same key into one undo step until end_gesture().
  std::expected<CommandResult, ToolError> run(const Invocation& call, const std::string& coalesce_key = {});

  // Typed helpers for widget-style edits (all go through run()).
  std::expected<CommandResult, ToolError> set_property(NodeId node, ComponentId component,
                                                       const PropertyDescriptor& property, const PropertyValue& value);
  std::expected<CommandResult, ToolError> set_transform(NodeId node, const Transform2D& transform);

  std::expected<void, ToolError> undo();
  std::expected<void, ToolError> redo();
  bool can_undo() const noexcept { return history_.can_undo(); }
  bool can_redo() const noexcept { return history_.can_redo(); }

  void end_gesture() { history_.end_gesture(); }

  // True when the scene differs from the last save point (undoing back to the
  // save point makes it false again). Edits made outside commands are not tracked here.
  bool modified() const noexcept { return !history_.at_save_point(); }
  void mark_saved() { history_.mark_saved(); }

  ProjectContext& context() noexcept { return context_; }
  const CommandRegistry& registry() const noexcept { return registry_; }

 private:
  ProjectContext& context_;
  const CommandRegistry& registry_;
  CommandHistory history_;
};

}  // namespace arcane::tooling
