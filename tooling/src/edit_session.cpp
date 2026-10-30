#include "arcane_tooling/edit_session.hpp"

#include "property_json.hpp"

namespace arcane::tooling {

EditSession::EditSession(ProjectContext& context, const CommandRegistry& registry)
    : context_(context), registry_(registry) {}

std::expected<CommandResult, ToolError> EditSession::run(const Invocation& call, const std::string& coalesce_key) {
  const std::uint64_t next_id_before = context_.scene().next_id();
  auto result = registry_.execute(context_, call);
  if (result) history_.record(call, *result, coalesce_key, next_id_before);
  return result;
}

std::expected<CommandResult, ToolError> EditSession::set_property(NodeId node, ComponentId component,
                                                                  const PropertyDescriptor& property,
                                                                  const PropertyValue& value) {
  Invocation call{"prop.set",
                  {{"node", "id:" + std::to_string(node)},
                   {"component", component},
                   {"name", property.name},
                   {"value", property_to_json(property.type, value)}}};
  return run(call, "prop:" + std::to_string(node) + ":" + std::to_string(component) + ":" + property.name);
}

std::expected<CommandResult, ToolError> EditSession::set_transform(NodeId node, const Transform2D& transform) {
  Invocation call{"node.set_transform",
                  {{"node", "id:" + std::to_string(node)},
                   {"position", {transform.position.x, transform.position.y}},
                   {"rotation_radians", transform.rotation_radians},
                   {"scale", {transform.scale.x, transform.scale.y}}}};
  return run(call, "transform:" + std::to_string(node));
}

std::expected<void, ToolError> EditSession::undo() { return history_.undo(registry_, context_); }
std::expected<void, ToolError> EditSession::redo() { return history_.redo(registry_, context_); }

}  // namespace arcane::tooling
