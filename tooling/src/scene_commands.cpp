// scene.*, node.* and component.* commands (docs/commands.md): structural edits of the open scene.
// Every edit that is not trivially invertible returns a `scene.restore` inverse holding the canonical
// scene text from before the call, so dry-run, persist rollback and undo share one mechanism.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "arcane_tooling/command.hpp"
#include "property_json.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, const ProjectContext& context, std::string path = {},
               std::string hint = {}) {
  return ToolError{code, std::move(message), context.scene_file(), std::move(path), std::move(hint)};
}

using Result = std::expected<CommandResult, ToolError>;

std::string node_path(const Node& node) {
  std::string path;
  for (const Node* n = &node; n; n = n->parent()) path = "/" + n->name() + path;
  return path;
}

std::string id_selector(const Node& node) { return "id:" + std::to_string(node.id()); }

Json node_ref(const Node& node) {
  return {{"id", node.id()}, {"name", node.name()}, {"path", node_path(node)}, {"selector", id_selector(node)}};
}

Json components_json(const Node& node) {
  Json list = Json::array();
  for (const auto& c : node.components()) list.push_back({{"id", c->id()}, {"type", c->type_name()}});
  return list;
}

Json vec_json(Vec2 v) { return Json::array({v.x, v.y}); }

Json tree_json(const Node& node, int depth) {
  Json out = {{"id", node.id()}, {"name", node.name()}, {"components", components_json(node)}};
  if (node.prefab_link()) out["prefab"] = node.prefab_link()->asset;
  Json children = Json::array();
  if (depth != 0) {
    for (const auto& child : node.children()) children.push_back(tree_json(*child, depth - 1));
  }
  out["children"] = children;
  if (depth == 0 && !node.children().empty()) out["children_omitted"] = node.children().size();
  return out;
}

void walk(const Node& node, const std::function<void(const Node&)>& visit) {
  visit(node);
  for (const auto& child : node.children()) walk(*child, visit);
}

void walk_scene(Scene& scene, const std::function<void(const Node&)>& visit) {
  for (const auto& root : scene.roots()) walk(*root, visit);
}

std::expected<std::string, ToolError> snapshot(ProjectContext& context) {
  auto text = context.scene().save_canonical_json(context.registry());
  if (!text) return std::unexpected(fail("VALUE_INVALID", text.error(), context));
  return std::move(*text);
}

Invocation restore_inverse(std::string text) { return {"scene.restore", {{"text", std::move(text)}}}; }

std::expected<void, ToolError> check_name(const ProjectContext& context, const std::string& name) {
  if (name.empty() || name.find('/') != std::string::npos) {
    return std::unexpected(fail("VALUE_INVALID", "node names must be non-empty and must not contain '/'", context,
                                "/name", "'/' separates names in path: selectors"));
  }
  return {};
}

std::expected<float, ToolError> finite_float(const ProjectContext& context, const Json& j, const std::string& path) {
  if (!j.is_number() || !std::isfinite(j.get<double>()) || std::fabs(j.get<double>()) > 3.0e38) {
    return std::unexpected(fail("VALUE_INVALID", "expected a finite number", context, path));
  }
  return static_cast<float>(j.get<double>());
}

std::expected<Vec2, ToolError> vec2_arg(const ProjectContext& context, const Json& j, const std::string& path) {
  if (!j.is_array() || j.size() != 2) return std::unexpected(fail("VALUE_INVALID", "expected [x, y]", context, path));
  auto x = finite_float(context, j[0], path + "/0");
  if (!x) return std::unexpected(x.error());
  auto y = finite_float(context, j[1], path + "/1");
  if (!y) return std::unexpected(y.error());
  return Vec2{*x, *y};
}

// Nodes that are part of a prefab instance are edited with prefab.* commands.
std::expected<void, ToolError> refuse_linked(const ProjectContext& context, const Node& node, const char* what) {
  if (node.prefab_link()) {
    return std::unexpected(fail("VALUE_INVALID",
                                std::string("node ") + std::to_string(node.id()) + " belongs to a prefab instance; cannot " + what,
                                context, "/node", "Use prefab.remove, prefab.break_link or prefab.override"));
  }
  return {};
}

const Json kNodeArg = {{"type", "string"}};
const Json kVec2Arg = {{"type", "array"}};

Json schema(Json properties, std::initializer_list<const char*> required = {}) {
  Json req = Json::array();
  for (const char* name : required) req.push_back(name);
  return {{"type", "object"}, {"properties", std::move(properties)}, {"required", req}, {"additionalProperties", false}};
}

Json transform_json(const Node& node) {
  const Transform2D& t = node.local_transform();
  return {{"position", vec_json(t.position)}, {"rotation_radians", t.rotation_radians}, {"scale", vec_json(t.scale)}};
}

// Copies `source` under `parent` (null = root) with fresh IDs, including components and properties.
Node* copy_subtree(ProjectContext& context, const Node& source, Node* parent, const std::string& name,
                   std::map<NodeId, NodeId>& ids, std::vector<Component*>& copied) {
  Scene& scene = context.scene();
  Node* node = scene.create_node(name, parent);
  node->local_transform() = source.local_transform();
  ids[source.id()] = node->id();
  for (const auto& component : source.components()) {
    Component* made = scene.add_component(node, component->type_name(), context.registry());
    if (!made) continue;
    if (const auto* descriptors = context.registry().properties_of(component->type_name())) {
      for (const auto& d : *descriptors) d.set(*made, d.get(*component));
    }
    copied.push_back(made);
  }
  for (const auto& child : source.children()) copy_subtree(context, *child, node, child->name(), ids, copied);
  return node;
}

}  // namespace

void register_scene_commands(CommandRegistry& registry) {
  registry.add(
      {"scene.tree", "Print the node hierarchy of the open scene (ids, names, components); `node` starts at a subtree.",
       schema({{"node", kNodeArg}, {"depth", {{"type", "integer"}, {"minimum", 0}}}}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> Result {
        const int depth = args.value("depth", -1);
        Json roots = Json::array();
        if (args.contains("node")) {
          auto node = resolve_node(context, args["node"].get<std::string>());
          if (!node) return std::unexpected(node.error());
          roots.push_back(tree_json(**node, depth));
        } else {
          for (const auto& root : context.scene().roots()) roots.push_back(tree_json(*root, depth));
        }
        return CommandResult{{{"scene", context.scene_file()}, {"nodes", roots}}, std::nullopt};
      });

  registry.add({"scene.query", "Summarize the open scene: node and component counts, component types in use, next id.",
                schema({}), {{"type", "object"}}, false},
               [](ProjectContext& context, const Json&) -> Result {
                 std::size_t nodes = 0, components = 0, roots = context.scene().roots().size();
                 std::map<std::string, std::size_t> types;
                 walk_scene(context.scene(), [&](const Node& node) {
                   ++nodes;
                   for (const auto& c : node.components()) {
                     ++components;
                     ++types[c->type_name()];
                   }
                 });
                 return CommandResult{{{"scene", context.scene_file()},
                                       {"nodes", nodes},
                                       {"roots", roots},
                                       {"components", components},
                                       {"component_types", types},
                                       {"next_id", context.scene().next_id()}},
                                      std::nullopt};
               });

  registry.add(
      {"scene.restore",
       "Internal inverse of structural edits: replaces the open scene with a canonical scene v2 text. Node and "
       "component pointers from before the call are invalid afterwards.",
       schema({{"text", {{"type", "string"}}}}, {"text"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto before = snapshot(context);
        if (!before) return std::unexpected(before.error());
        auto scene = Scene::load_from_json(args["text"].get<std::string>(), context.registry());
        if (!scene) return std::unexpected(fail("SCHEMA_INVALID", scene.error(), context, "/text"));
        context.scene() = std::move(*scene);
        return CommandResult{{{"restored", true}}, restore_inverse(std::move(*before))};
      });

  registry.add(
      {"node.info", "Describe one node: path, parent, transform, children, components and prefab link.",
       schema({{"node", kNodeArg}}, {"node"}), {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Json children = Json::array();
        for (const auto& child : (*node)->children()) children.push_back(node_ref(*child));
        Json info = node_ref(**node);
        info["parent"] = (*node)->parent() ? Json((*node)->parent()->id()) : Json(nullptr);
        info["transform"] = transform_json(**node);
        info["components"] = components_json(**node);
        info["children"] = children;
        info["persistent"] = (*node)->persistent();
        if ((*node)->prefab_link()) {
          info["prefab"] = {{"asset", (*node)->prefab_link()->asset}, {"instance_id", (*node)->prefab_link()->instance_id}};
        }
        return CommandResult{info, std::nullopt};
      });

  registry.add(
      {"node.find", "Find nodes by exact name, name substring or attached component type; `under` limits the subtree.",
       schema({{"name", {{"type", "string"}}},
               {"name_contains", {{"type", "string"}}},
               {"component", {{"type", "string"}}},
               {"under", kNodeArg}}),
       {{"type", "object"}}, false},
      [](ProjectContext& context, const Json& args) -> Result {
        Json found = Json::array();
        auto visit = [&](const Node& node) {
          if (args.contains("name") && node.name() != args["name"].get<std::string>()) return;
          if (args.contains("name_contains") && node.name().find(args["name_contains"].get<std::string>()) == std::string::npos) return;
          if (args.contains("component") && !node.find_component(args["component"].get<std::string>())) return;
          found.push_back(node_ref(node));
        };
        if (args.contains("under")) {
          auto under = resolve_node(context, args["under"].get<std::string>());
          if (!under) return std::unexpected(under.error());
          walk(**under, visit);
        } else {
          walk_scene(context.scene(), visit);
        }
        return CommandResult{{{"count", found.size()}, {"nodes", found}}, std::nullopt};
      });

  registry.add(
      {"node.add", "Create a node (a root, or under `parent`) with an optional local transform; the id is allocated.",
       schema({{"name", {{"type", "string"}}},
               {"parent", kNodeArg},
               {"position", kVec2Arg},
               {"rotation_radians", {{"type", "number"}}},
               {"scale", kVec2Arg}},
              {"name"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        const std::string name = args["name"].get<std::string>();
        if (auto ok = check_name(context, name); !ok) return std::unexpected(ok.error());
        Node* parent = nullptr;
        if (args.contains("parent")) {
          auto resolved = resolve_node(context, args["parent"].get<std::string>());
          if (!resolved) return std::unexpected(resolved.error());
          parent = *resolved;
        }
        Transform2D transform;
        if (args.contains("position")) {
          auto v = vec2_arg(context, args["position"], "/position");
          if (!v) return std::unexpected(v.error());
          transform.position = *v;
        }
        if (args.contains("scale")) {
          auto v = vec2_arg(context, args["scale"], "/scale");
          if (!v) return std::unexpected(v.error());
          transform.scale = *v;
        }
        if (args.contains("rotation_radians")) {
          auto r = finite_float(context, args["rotation_radians"], "/rotation_radians");
          if (!r) return std::unexpected(r.error());
          transform.rotation_radians = *r;
        }
        Node* node = context.scene().create_node(name, parent);
        node->local_transform() = transform;
        return CommandResult{node_ref(*node), Invocation{"node.remove", {{"node", id_selector(*node)}}}};
      });

  registry.add(
      {"node.remove",
       "Remove a node and its subtree. Refused for prefab-instance nodes and while a node outside the subtree "
       "references it through an entity_id property.",
       schema({{"node", kNodeArg}}, {"node"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto resolved = resolve_node(context, args["node"].get<std::string>());
        if (!resolved) return std::unexpected(resolved.error());
        Node& node = **resolved;
        std::set<NodeId> doomed;
        bool linked = false;
        walk(node, [&](const Node& n) {
          doomed.insert(n.id());
          linked = linked || n.prefab_link().has_value();
        });
        if (linked) {
          return std::unexpected(fail("VALUE_INVALID", "the subtree contains prefab-instance nodes; cannot remove it", context,
                                      "/node", "Use prefab.remove or prefab.break_link"));
        }
        std::optional<ToolError> referenced;
        walk_scene(context.scene(), [&](const Node& n) {
          if (referenced || doomed.count(n.id())) return;
          for (const auto& c : n.components()) {
            const auto* props = context.registry().properties_of(c->type_name());
            if (!props) continue;
            for (const auto& p : *props) {
              if (p.type != PropertyType::EntityId) continue;
              const auto target = std::get<AssetId>(p.get(*c));
              if (target != 0 && doomed.count(target)) {
                referenced = fail("CONFLICT",
                                  "node " + std::to_string(target) + " is referenced by " + p.name + " of component " +
                                      std::to_string(c->id()) + " on node " + std::to_string(n.id()),
                                  context, "/node", "Clear the reference with prop set first");
                return;
              }
            }
          }
        });
        if (referenced) return std::unexpected(*referenced);
        auto before = snapshot(context);
        if (!before) return std::unexpected(before.error());
        const Json ref = node_ref(node);
        context.scene().destroy_node(node.id());
        context.scene().flush_destroyed();
        return CommandResult{{{"removed", ref}, {"nodes", doomed.size()}}, restore_inverse(std::move(*before))};
      });

  registry.add(
      {"node.rename", "Rename a node (ids stay). Names must not contain '/'.",
       schema({{"node", kNodeArg}, {"name", {{"type", "string"}}}}, {"node", "name"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        const std::string name = args["name"].get<std::string>();
        if (auto ok = check_name(context, name); !ok) return std::unexpected(ok.error());
        const std::string previous = (*node)->name();
        if (previous == name) return CommandResult{{{"changed", false}, {"name", name}}, std::nullopt};
        (*node)->set_name(name);
        return CommandResult{{{"changed", true}, {"previous", previous}, {"name", name}},
                             Invocation{"node.rename", {{"node", id_selector(**node)}, {"name", previous}}}};
      });

  registry.add(
      {"node.reparent",
       "Move a node (with its subtree) under `parent`, or make it a root when `parent` is omitted. It becomes the "
       "last child. Refused for cycles and non-root prefab-instance nodes.",
       schema({{"node", kNodeArg}, {"parent", kNodeArg}}, {"node"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Node* parent = nullptr;
        if (args.contains("parent")) {
          auto resolved = resolve_node(context, args["parent"].get<std::string>());
          if (!resolved) return std::unexpected(resolved.error());
          parent = *resolved;
        }
        if ((*node)->prefab_link() && (*node)->prefab_link()->instance_id != (*node)->id()) {
          if (auto ok = refuse_linked(context, **node, "move it out of its instance"); !ok) return std::unexpected(ok.error());
        }
        auto before = snapshot(context);
        if (!before) return std::unexpected(before.error());
        if (!context.scene().reparent(*node, parent)) {
          return std::unexpected(fail("VALUE_INVALID", "cannot move a node under itself or its own descendant", context, "/parent"));
        }
        return CommandResult{node_ref(**node), restore_inverse(std::move(*before))};
      });

  registry.add(
      {"node.duplicate",
       "Deep-copy a node subtree (components and property values) with new ids; entity_id properties that pointed "
       "inside the subtree point at the copy. Refused for prefab-instance nodes.",
       schema({{"node", kNodeArg}, {"name", {{"type", "string"}}}, {"parent", kNodeArg}}, {"node"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto source = resolve_node(context, args["node"].get<std::string>());
        if (!source) return std::unexpected(source.error());
        bool linked = false;
        walk(**source, [&](const Node& n) { linked = linked || n.prefab_link().has_value(); });
        if (linked) {
          if (auto ok = refuse_linked(context, **source, "duplicate it"); !ok) return std::unexpected(ok.error());
        }
        Node* parent = (*source)->parent();
        if (args.contains("parent")) {
          auto resolved = resolve_node(context, args["parent"].get<std::string>());
          if (!resolved) return std::unexpected(resolved.error());
          parent = *resolved;
          for (const Node* n = parent; n; n = n->parent()) {
            if (n == *source) return std::unexpected(fail("VALUE_INVALID", "cannot duplicate a node into itself", context, "/parent"));
          }
        }
        const std::string name = args.value("name", (*source)->name() + "_copy");
        if (auto ok = check_name(context, name); !ok) return std::unexpected(ok.error());
        std::map<NodeId, NodeId> ids;
        std::vector<Component*> copied;
        Node* copy = copy_subtree(context, **source, parent, name, ids, copied);
        for (Component* component : copied) {
          const auto* props = context.registry().properties_of(component->type_name());
          if (!props) continue;
          for (const auto& p : *props) {
            if (p.type != PropertyType::EntityId) continue;
            const auto target = std::get<AssetId>(p.get(*component));
            auto mapped = ids.find(target);
            if (mapped != ids.end()) p.set(*component, PropertyValue{static_cast<AssetId>(mapped->second)});
          }
        }
        Json result = node_ref(*copy);
        result["nodes"] = ids.size();
        return CommandResult{result, Invocation{"node.remove", {{"node", id_selector(*copy)}}}};
      });

  registry.add(
      {"node.set_persistent",
       "Set the authored `persistent` flag of a root node: SceneManager carries such roots over scene switches "
       "(docs/formats/scene.md). Refused for non-root nodes.",
       schema({{"node", kNodeArg}, {"value", {{"type", "boolean"}}}}, {"node", "value"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        if (!args["value"].is_boolean()) return std::unexpected(fail("VALUE_INVALID", "expected true or false", context, "/value"));
        if ((*node)->parent()) {
          return std::unexpected(fail("VALUE_INVALID", "only a root node can be persistent", context, "/node"));
        }
        const bool previous = (*node)->persistent();
        const bool value = args["value"].get<bool>();
        (*node)->set_persistent(value);
        Json result = {{"changed", previous != value}, {"previous", previous}, {"persistent", value}};
        if (previous == value) return CommandResult{result, std::nullopt};
        return CommandResult{result, Invocation{"node.set_persistent", {{"node", id_selector(**node)}, {"value", previous}}}};
      });

  registry.add(
      {"node.set_transform", "Set a node's local transform fields; omitted fields keep their value.",
       schema({{"node", kNodeArg}, {"position", kVec2Arg}, {"rotation_radians", {{"type", "number"}}}, {"scale", kVec2Arg}},
              {"node"}),
       {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Transform2D next = (*node)->local_transform();
        if (args.contains("position")) {
          auto v = vec2_arg(context, args["position"], "/position");
          if (!v) return std::unexpected(v.error());
          next.position = *v;
        }
        if (args.contains("scale")) {
          auto v = vec2_arg(context, args["scale"], "/scale");
          if (!v) return std::unexpected(v.error());
          next.scale = *v;
        }
        if (args.contains("rotation_radians")) {
          auto r = finite_float(context, args["rotation_radians"], "/rotation_radians");
          if (!r) return std::unexpected(r.error());
          next.rotation_radians = *r;
        }
        const Json previous = transform_json(**node);
        (*node)->local_transform() = next;
        const Json now = transform_json(**node);
        Json result = {{"changed", previous != now}, {"previous", previous}, {"transform", now}};
        if (previous == now) return CommandResult{result, std::nullopt};
        Json inverse_args = previous;
        inverse_args["node"] = id_selector(**node);
        return CommandResult{result, Invocation{"node.set_transform", inverse_args}};
      });
}

void register_component_commands(CommandRegistry& registry) {
  registry.add({"component.types", "List the registered component types with their property names and types.",
                schema({}), {{"type", "object"}}, false},
               [](ProjectContext& context, const Json&) -> Result {
                 std::vector<std::string> names = context.registry().type_names();
                 std::sort(names.begin(), names.end());
                 Json types = Json::array();
                 for (const auto& name : names) {
                   Json props = Json::array();
                   if (const auto* list = context.registry().properties_of(name)) {
                     for (const auto& p : *list) props.push_back({{"name", p.name}, {"type", std::string(to_string(p.type))}});
                   }
                   types.push_back({{"type", name}, {"properties", props}});
                 }
                 return CommandResult{{{"types", types}}, std::nullopt};
               });

  registry.add({"component.list", "List the components attached to a node.", schema({{"node", kNodeArg}}, {"node"}),
                {{"type", "object"}}, false},
               [](ProjectContext& context, const Json& args) -> Result {
                 auto node = resolve_node(context, args["node"].get<std::string>());
                 if (!node) return std::unexpected(node.error());
                 return CommandResult{{{"node", node_ref(**node)}, {"components", components_json(**node)}}, std::nullopt};
               });

  registry.add(
      {"component.add", "Attach a new component of a registered type (default property values) to a node.",
       schema({{"node", kNodeArg}, {"type", {{"type", "string"}}}}, {"node", "type"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        const std::string type = args["type"].get<std::string>();
        if (!context.registry().contains(type)) {
          return std::unexpected(fail("COMPONENT_TYPE_UNKNOWN", "component type '" + type + "' is not registered", context,
                                      "/type", "Run component types to list registered types (is the module loaded?)"));
        }
        Component* component = context.scene().add_component(*node, type, context.registry());
        if (!component) return std::unexpected(fail("COMPONENT_TYPE_UNKNOWN", "could not create '" + type + "'", context, "/type"));
        return CommandResult{{{"component_id", component->id()}, {"type", type}, {"node", id_selector(**node)}},
                             Invocation{"component.remove", {{"node", id_selector(**node)}, {"component", component->id()}}}};
      });

  registry.add(
      {"component.remove", "Detach and destroy a component (by id, or by type name when attached once).",
       schema({{"node", kNodeArg}, {"component", {{"cli_text", true}}}}, {"node", "component"}), {{"type", "object"}}, true},
      [](ProjectContext& context, const Json& args) -> Result {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        auto component = resolve_component(context, **node, args["component"]);
        if (!component) return std::unexpected(component.error());
        auto before = snapshot(context);
        if (!before) return std::unexpected(before.error());
        Json result = {{"removed", {{"component_id", (*component)->id()}, {"type", (*component)->type_name()}}}};
        context.scene().remove_component(*component);
        return CommandResult{result, restore_inverse(std::move(*before))};
      });
}

}  // namespace arcane::tooling
