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
  // Edits of prefab instance nodes are recorded as overrides of the prefab value.
  const Node* target = context_.scene().find_node(node);
  const Component* owner = context_.scene().find_component(component);
  if (target && target->prefab_link() && owner) {
    Invocation call{"prefab.override",
                    {{"node", "id:" + std::to_string(node)},
                     {"component", owner->type_name()},
                     {"property", property.name},
                     {"value", property_to_json(property.type, value)}}};
    return run(call, "prop:" + std::to_string(node) + ":" + std::to_string(component) + ":" + property.name);
  }
  Invocation call{"prop.set",
                  {{"node", "id:" + std::to_string(node)},
                   {"component", component},
                   {"name", property.name},
                   {"value", property_to_json(property.type, value)}}};
  return run(call, "prop:" + std::to_string(node) + ":" + std::to_string(component) + ":" + property.name);
}

std::expected<CommandResult, ToolError> EditSession::set_transform(NodeId node, const Transform2D& transform) {
  const Node* target = context_.scene().find_node(node);
  if (target && target->prefab_link()) {
    // One prefab.override per changed field.
    const Transform2D& before = target->local_transform();
    const std::string selector = "id:" + std::to_string(node);
    std::expected<CommandResult, ToolError> last = CommandResult{};
    const auto set = [&](const char* property, Json value) {
      if (last) last = run({"prefab.override", {{"node", selector}, {"property", property}, {"value", std::move(value)}}},
                           "transform:" + std::to_string(node) + property);
    };
    if (transform.position != before.position) set("position", {transform.position.x, transform.position.y});
    if (transform.rotation_radians != before.rotation_radians) set("rotation_radians", transform.rotation_radians);
    if (transform.scale != before.scale) set("scale", {transform.scale.x, transform.scale.y});
    return last;
  }
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
