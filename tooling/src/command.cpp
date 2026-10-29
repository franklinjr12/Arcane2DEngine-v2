#include "arcane_tooling/command.hpp"

#include <algorithm>

namespace arcane::tooling {

namespace {

ToolError args_error(const std::string& message, const std::string& path) {
  return ToolError{"ARGS_INVALID", message, "", path, "Run arcane schema --command NAME for the argument schema"};
}

bool type_matches(const std::string& type, const Json& value) {
  if (type == "string") return value.is_string();
  if (type == "boolean") return value.is_boolean();
  if (type == "integer") return value.is_number_integer();
  if (type == "number") return value.is_number();
  if (type == "array") return value.is_array();
  if (type == "object") return value.is_object();
  return false;
}

std::expected<void, ToolError> validate_at(const Json& schema, const Json& value, const std::string& path) {
  if (schema.contains("type")) {
    const std::string type = schema["type"].get<std::string>();
    if (!type_matches(type, value)) {
      return std::unexpected(args_error("expected " + type, path.empty() ? "/" : path));
    }
  }
  if (schema.contains("enum")) {
    const auto& allowed = schema["enum"];
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
      return std::unexpected(args_error("value is not one of the allowed values", path.empty() ? "/" : path));
    }
  }
  if (value.is_number() && schema.contains("minimum") && value.get<double>() < schema["minimum"].get<double>()) {
    return std::unexpected(args_error("value is below the minimum", path));
  }
  if (value.is_number() && schema.contains("maximum") && value.get<double>() > schema["maximum"].get<double>()) {
    return std::unexpected(args_error("value is above the maximum", path));
  }
  if (value.is_object()) {
    const Json properties = schema.value("properties", Json::object());
    if (schema.contains("required")) {
      for (const auto& name : schema["required"]) {
        if (!value.contains(name.get<std::string>())) {
          return std::unexpected(args_error("missing required argument '" + name.get<std::string>() + "'",
                                            path + "/" + name.get<std::string>()));
        }
      }
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
      auto prop = properties.find(it.key());
      if (prop == properties.end()) {
        if (schema.value("additionalProperties", true)) continue;
        return std::unexpected(args_error("unknown argument '" + it.key() + "'", path + "/" + it.key()));
      }
      auto inner = validate_at(*prop, it.value(), path + "/" + it.key());
      if (!inner) return inner;
    }
  }
  return {};
}

std::expected<CommandResult, ToolError> run_guarded(const CommandFn& fn, ProjectContext& context, const Json& args) {
  try {
    return fn(context, args);
  } catch (const std::exception& e) {
    return std::unexpected(ToolError{"MODULE_FAILURE", std::string("command threw: ") + e.what(), "", "", ""});
  } catch (...) {
    return std::unexpected(ToolError{"MODULE_FAILURE", "command threw a non-standard exception", "", "", ""});
  }
}

}  // namespace

std::expected<void, ToolError> validate_args(const Json& schema, const Json& value) {
  return validate_at(schema, value, "");
}

void CommandRegistry::add(CommandSpec spec, CommandFn fn) {
  std::string name = spec.name;
  entries_[name] = Entry{std::move(spec), std::move(fn)};
}

const CommandSpec* CommandRegistry::find(const std::string& name) const {
  auto it = entries_.find(name);
  return it == entries_.end() ? nullptr : &it->second.spec;
}

void CommandRegistry::mark_immediate_write(const std::string& name) {
  auto it = entries_.find(name);
  if (it != entries_.end()) it->second.spec.immediate_write = true;
}

std::vector<const CommandSpec*> CommandRegistry::list() const {
  std::vector<const CommandSpec*> out;
  for (const auto& [name, entry] : entries_) out.push_back(&entry.spec);
  return out;
}

std::expected<CommandResult, ToolError> CommandRegistry::execute(ProjectContext& context, const Invocation& call,
                                                                 const ExecuteOptions& options) const {
  auto it = entries_.find(call.command);
  if (it == entries_.end()) {
    return std::unexpected(ToolError{"UNKNOWN_COMMAND", "Unknown command '" + call.command + "'", "", "",
                                     "Run arcane docs for the command list"});
  }
  const Entry& entry = it->second;
  if (auto valid = validate_args(entry.spec.args_schema, call.args); !valid) {
    return std::unexpected(valid.error());
  }

  // Module code runs inside commands (property setters); a throwing module
  // must not take the caller down or leave a half-applied command behind.
  auto result = run_guarded(entry.fn, context, call.args);
  if (!result) return result;
  if (!entry.spec.mutating || !result->inverse) return result;  // read or no-op: nothing to persist

  // Restores the state before this call by running the inverse directly
  // (no validation round trip, no dirty marking, no saving).
  auto roll_back = [&] {
    auto inverse = entries_.find(result->inverse->command);
    if (inverse != entries_.end()) (void)run_guarded(inverse->second.fn, context, result->inverse->args);
  };
  if (options.dry_run) {
    roll_back();
    return result;
  }
  if (options.persist) {
    // save() clears the dirty flag; on failure memory goes back to the saved state.
    if (auto saved = context.save(); !saved) {
      roll_back();
      return std::unexpected(saved.error());
    }
  } else {
    context.mark_dirty();
  }
  return result;
}

void CommandHistory::record(const Invocation& forward, const CommandResult& result, const std::string& coalesce_key,
                            std::uint64_t next_id_before) {
  if (!result.inverse) return;
  // Merging rewrites the top entry, which would change what the save point means.
  if (!coalesce_key.empty() && coalesce_key == open_key_ && !undo_.empty() && saved_ != undo_.size()) {
    undo_.back().forward = forward;
    return;
  }
  if (saved_ != kUnreachable && saved_ > undo_.size()) saved_ = kUnreachable;  // saved state lived in the redo stack
  undo_.push_back(Entry{forward, *result.inverse, next_id_before});
  redo_.clear();
  open_key_ = coalesce_key;
}

std::expected<void, ToolError> CommandHistory::undo(const CommandRegistry& registry, ProjectContext& context) {
  if (undo_.empty()) return std::unexpected(ToolError{"CONFLICT", "Nothing to undo", "", "", ""});
  auto done = registry.execute(context, undo_.back().inverse);
  if (!done) return std::unexpected(done.error());
  if (undo_.back().next_id_before != 0) context.scene().set_next_id(undo_.back().next_id_before);
  redo_.push_back(std::move(undo_.back()));
  undo_.pop_back();
  open_key_.clear();
  return {};
}

std::expected<void, ToolError> CommandHistory::redo(const CommandRegistry& registry, ProjectContext& context) {
  if (redo_.empty()) return std::unexpected(ToolError{"CONFLICT", "Nothing to redo", "", "", ""});
  auto done = registry.execute(context, redo_.back().forward);
  if (!done) return std::unexpected(done.error());
  undo_.push_back(std::move(redo_.back()));
  redo_.pop_back();
  open_key_.clear();
  return {};
}

}  // namespace arcane::tooling
