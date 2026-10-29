#include <charconv>
#include <cmath>
#include <limits>

#include "arcane_tooling/command.hpp"
#include "property_json.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, const ProjectContext& context, std::string path = {},
               std::string hint = {}) {
  return ToolError{code, std::move(message), context.scene_file(), std::move(path), std::move(hint)};
}

// Floats are stored as binary32; print the shortest text that reads back as
// the same float so results show 0.1, not 0.10000000149011612.
Json float_json(float value) {
  char buffer[64];
  auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value);
  return ec == std::errc() ? Json(std::stod(std::string(buffer, end))) : Json(static_cast<double>(value));
}

}  // namespace

Json property_to_json(PropertyType type, const PropertyValue& value) {
  switch (type) {
    case PropertyType::Bool: return std::get<bool>(value);
    case PropertyType::Int: return std::get<int>(value);
    case PropertyType::Float: return float_json(std::get<float>(value));
    case PropertyType::Vec2: {
      const Vec2& v = std::get<Vec2>(value);
      return Json::array({float_json(v.x), float_json(v.y)});
    }
    case PropertyType::Color: {
      const Color& c = std::get<Color>(value);
      return Json::array({float_json(c.r), float_json(c.g), float_json(c.b), float_json(c.a)});
    }
    case PropertyType::String: return std::get<std::string>(value);
    case PropertyType::AssetId:
    case PropertyType::EntityId: return std::get<AssetId>(value);
  }
  return nullptr;
}

namespace {

Json value_to_json(const PropertyDescriptor& property, const PropertyValue& value) {
  return property_to_json(property.type, value);
}

std::expected<float, ToolError> to_float(const Json& j, const ProjectContext& context, const std::string& path) {
  if (!j.is_number()) return std::unexpected(fail("VALUE_INVALID", "expected a number", context, path));
  const double d = j.get<double>();
  if (!std::isfinite(d) || std::fabs(d) > std::numeric_limits<float>::max()) {
    return std::unexpected(fail("VALUE_INVALID", "number is out of range for a float", context, path));
  }
  return static_cast<float>(d);
}

std::expected<std::vector<float>, ToolError> float_array(const Json& j, std::size_t count, const char* what,
                                                         const ProjectContext& context) {
  if (!j.is_array() || j.size() != count) {
    return std::unexpected(fail("VALUE_INVALID", std::string("expected ") + what, context, "/value"));
  }
  std::vector<float> out;
  for (std::size_t i = 0; i < count; ++i) {
    auto f = to_float(j[i], context, "/value/" + std::to_string(i));
    if (!f) return std::unexpected(f.error());
    out.push_back(*f);
  }
  return out;
}

}  // namespace

// Converts and validates a typed JSON value before anything is touched.
std::expected<PropertyValue, ToolError> property_from_json(PropertyType property_type, const std::string& property_name,
                                                         const Json& j, ProjectContext& context) {
  const std::string type(to_string(property_type));
  auto wrong = [&](const char* expected) {
    return std::unexpected(fail("VALUE_INVALID", "property '" + property_name + "' is " + type + ": expected " + expected,
                                context, "/value"));
  };
  switch (property_type) {
    case PropertyType::Bool:
      if (!j.is_boolean()) return wrong("true or false");
      return PropertyValue(j.get<bool>());
    case PropertyType::Int:
      if (!j.is_number_integer() || j.get<long long>() < std::numeric_limits<int>::min() ||
          j.get<long long>() > std::numeric_limits<int>::max()) {
        return wrong("a 32-bit integer");
      }
      return PropertyValue(j.get<int>());
    case PropertyType::Float: {
      if (!j.is_number()) return wrong("a number");
      auto f = to_float(j, context, "/value");
      if (!f) return std::unexpected(f.error());
      return PropertyValue(*f);
    }
    case PropertyType::Vec2: {
      if (!j.is_array()) return wrong("[x, y]");
      auto v = float_array(j, 2, "[x, y]", context);
      if (!v) return std::unexpected(v.error());
      return PropertyValue(Vec2{(*v)[0], (*v)[1]});
    }
    case PropertyType::Color: {
      if (!j.is_array()) return wrong("[r, g, b, a]");
      auto v = float_array(j, 4, "[r, g, b, a]", context);
      if (!v) return std::unexpected(v.error());
      return PropertyValue(Color{(*v)[0], (*v)[1], (*v)[2], (*v)[3]});
    }
    case PropertyType::String:
      if (!j.is_string()) return wrong("a string");
      return PropertyValue(j.get<std::string>());
    case PropertyType::AssetId:
    case PropertyType::EntityId: {
      if (!j.is_number_integer() || (!j.is_number_unsigned() && j.get<long long>() < 0)) {
        return wrong("an unsigned integer");
      }
      const AssetId id = j.get<AssetId>();
      if (property_type == PropertyType::EntityId && id != 0 && !context.scene().find_node(id)) {
        return std::unexpected(fail("NODE_NOT_FOUND", "no node with id " + std::to_string(id), context, "/value"));
      }
      return PropertyValue(id);
    }
  }
  return wrong("a supported type");
}

namespace {

std::expected<PropertyValue, ToolError> json_to_value(const PropertyDescriptor& property, const Json& j,
                                                      ProjectContext& context) {
  return property_from_json(property.type, property.name, j, context);
}

// "component" is a component id (integer) or a type name that must be
// attached to the node exactly once.
std::expected<Component*, ToolError> resolve_component(ProjectContext& context, Node& node, const Json& selector) {
  if (selector.is_number_unsigned()) {
    Component* found = context.scene().find_component(selector.get<ComponentId>());
    if (!found || found->node() != &node) {
      return std::unexpected(fail("COMPONENT_NOT_FOUND",
                                  "node " + std::to_string(node.id()) + " has no component " + selector.dump(),
                                  context, "/component"));
    }
    return found;
  }
  if (!selector.is_string()) {
    return std::unexpected(fail("ARGS_INVALID", "component must be a component id or a type name", context,
                                "/component"));
  }
  const std::string type = selector.get<std::string>();
  Component* found = nullptr;
  for (const auto& component : node.components()) {
    if (component->type_name() != type) continue;
    if (found) {
      return std::unexpected(fail("SELECTOR_AMBIGUOUS", "node has several '" + type + "' components", context,
                                  "/component", "Select by component id instead"));
    }
    found = component.get();
  }
  if (!found) {
    return std::unexpected(fail("COMPONENT_NOT_FOUND", "node " + std::to_string(node.id()) + " has no '" + type + "'",
                                context, "/component"));
  }
  return found;
}

std::expected<const std::vector<PropertyDescriptor>*, ToolError> descriptors_of(ProjectContext& context,
                                                                                  const Component& component) {
  const auto* props = context.registry().properties_of(component.type_name());
  if (!props) {
    return std::unexpected(fail("COMPONENT_TYPE_UNKNOWN", "component type '" + component.type_name() + "' is not registered",
                                context, "/component"));
  }
  return props;
}

std::expected<const PropertyDescriptor*, ToolError> find_property(ProjectContext& context, const Component& component,
                                                                 const std::string& name) {
  auto props = descriptors_of(context, component);
  if (!props) return std::unexpected(props.error());
  for (const auto& p : **props) {
    if (p.name == name) return &p;
  }
  return std::unexpected(fail("PROPERTY_UNKNOWN", "component '" + component.type_name() + "' has no property '" + name + "'",
                              context, "/name", "Run prop.list to see the registered properties"));
}

struct Target {
  Node* node;
  Component* component;
};

std::expected<Target, ToolError> resolve_target(ProjectContext& context, const Json& args) {
  auto node = resolve_node(context, args["node"].get<std::string>());
  if (!node) return std::unexpected(node.error());
  auto component = resolve_component(context, **node, args["component"]);
  if (!component) return std::unexpected(component.error());
  return Target{*node, *component};
}

const Json kTarget = {{"node", {{"type", "string"}}}, {"component", Json::object()}};

Json args_schema(std::initializer_list<const char*> extra_keys, std::initializer_list<const char*> required) {
  Json properties = kTarget;
  for (const char* key : extra_keys) {
    properties[key] = std::string(key) == "name" ? Json{{"type", "string"}} : Json::object();
  }
  Json req = Json::array({"node", "component"});
  for (const char* key : required) req.push_back(key);
  return {{"type", "object"}, {"properties", properties}, {"required", req}, {"additionalProperties", false}};
}

}  // namespace

std::expected<Node*, ToolError> resolve_node(ProjectContext& context, const std::string& selector) {
  auto bad = [&](const char* message) {
    return std::unexpected(fail("ARGS_INVALID", message, context, "/node", "Use id:42 or path:/Root/Player"));
  };
  if (selector.rfind("id:", 0) == 0) {
    const std::string text = selector.substr(3);
    NodeId id = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), id);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size()) return bad("node id must be an unsigned integer");
    Node* node = context.scene().find_node(id);
    if (!node) return std::unexpected(fail("NODE_NOT_FOUND", "no node with id " + text, context, "/node"));
    return node;
  }
  if (selector.rfind("path:/", 0) == 0) {
    Node* current = nullptr;
    std::size_t start = 6;
    while (start <= selector.size()) {
      std::size_t end = selector.find('/', start);
      if (end == std::string::npos) end = selector.size();
      const std::string name = selector.substr(start, end - start);
      if (name.empty()) return bad("empty name in node path");
      std::vector<Node*> matches;
      auto collect = [&](const std::vector<std::unique_ptr<Node>>& nodes) {
        for (const auto& candidate : nodes) {
          if (candidate->name() == name) matches.push_back(candidate.get());
        }
      };
      collect(current ? current->children() : context.scene().roots());
      const std::size_t count = matches.size();
      Node* match = count == 1 ? matches[0] : nullptr;
      if (count == 0) return std::unexpected(fail("NODE_NOT_FOUND", "no node '" + name + "' on path " + selector, context, "/node"));
      if (count > 1) {
        return std::unexpected(fail("SELECTOR_AMBIGUOUS", "several nodes named '" + name + "' on path " + selector,
                                    context, "/node", "Select by id:N instead"));
      }
      current = match;
      start = end + 1;
    }
    return current;
  }
  return bad("selector must start with id: or path:/");
}

void register_property_commands(CommandRegistry& registry) {
  registry.add(
      {"prop.list", "List the registered properties of a component with their types and current values.",
       args_schema({}, {}),
       {{"type", "object"}, {"properties", {{"properties", {{"type", "array"}}}}}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto target = resolve_target(context, args);
        if (!target) return std::unexpected(target.error());
        auto props = descriptors_of(context, *target->component);
        if (!props) return std::unexpected(props.error());
        Json list = Json::array();
        for (const auto& p : **props) {
          list.push_back({{"name", p.name},
                          {"type", std::string(to_string(p.type))},
                          {"value", value_to_json(p, p.get(*target->component))}});
        }
        return CommandResult{{{"component_id", target->component->id()},
                              {"type", target->component->type_name()},
                              {"properties", list}},
                             std::nullopt};
      });

  registry.add(
      {"prop.get", "Read one registered property of a component.", args_schema({"name"}, {"name"}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto target = resolve_target(context, args);
        if (!target) return std::unexpected(target.error());
        auto property = find_property(context, *target->component, args["name"].get<std::string>());
        if (!property) return std::unexpected(property.error());
        return CommandResult{{{"name", (*property)->name},
                              {"type", std::string(to_string((*property)->type))},
                              {"value", value_to_json(**property, (*property)->get(*target->component))}},
                             std::nullopt};
      });

  registry.add(
      {"prop.set",
       "Set one registered property. The value is JSON typed by the property (bool, int, float, [x,y], [r,g,b,a], string, "
       "unsigned id); a wrong type or unknown property changes nothing.",
       args_schema({"name", "value"}, {"name", "value"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto target = resolve_target(context, args);
        if (!target) return std::unexpected(target.error());
        auto property = find_property(context, *target->component, args["name"].get<std::string>());
        if (!property) return std::unexpected(property.error());
        auto value = json_to_value(**property, args["value"], context);
        if (!value) return std::unexpected(value.error());

        const Json before = value_to_json(**property, (*property)->get(*target->component));
        const Json after = value_to_json(**property, *value);
        Json result = {{"changed", before != after}, {"previous", before}, {"value", after}};
        if (before == after) return CommandResult{result, std::nullopt};  // idempotent: nothing to undo

        (*property)->set(*target->component, *value);
        // The inverse names the component by id, which stays valid for this scene's lifetime.
        Invocation inverse{"prop.set",
                           {{"node", "id:" + std::to_string(target->node->id())},
                            {"component", target->component->id()},
                            {"name", (*property)->name},
                            {"value", before}}};
        return CommandResult{result, inverse};
      });
}

}  // namespace arcane::tooling
