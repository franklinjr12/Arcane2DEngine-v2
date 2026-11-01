// prefab.* commands (docs/formats/prefab.md, docs/commands.md). Prefab files
// are written by prefab.save; scene instances keep full values plus an
// explicit override list that names prefab-local node IDs.
#include <filesystem>

#include "arcane/canonical_json.hpp"
#include "arcane/prefab.hpp"
#include "arcane_tooling/command.hpp"
#include "property_json.hpp"

namespace arcane::tooling {

namespace {

ToolError fail(const char* code, std::string message, const ProjectContext& context, std::string path = {},
               std::string hint = {}) {
  return ToolError{code, std::move(message), context.scene_file(), std::move(path), std::move(hint)};
}

bool valid_asset_path(const std::string& asset) {
  constexpr std::string_view kSuffix = ".prefab.json";
  if (asset.size() <= kSuffix.size() || asset.compare(asset.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
    return false;
  }
  if (asset.find('\\') != std::string::npos || asset[0] == '/' || (asset.size() > 1 && asset[1] == ':')) return false;
  std::size_t start = 0;
  while (start <= asset.size()) {
    std::size_t end = asset.find('/', start);
    if (end == std::string::npos) end = asset.size();
    const std::string segment = asset.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..") return false;
    start = end + 1;
  }
  return true;
}

std::expected<void, ToolError> check_asset(const ProjectContext& context, const std::string& asset) {
  if (valid_asset_path(asset)) return {};
  return std::unexpected(fail("ARGS_INVALID", "asset must be a project-relative '/' path ending in .prefab.json",
                              context, "/asset", "Example: prefabs/enemy.prefab.json"));
}

std::expected<Prefab, ToolError> load_prefab(ProjectContext& context, const std::string& asset) {
  if (auto ok = check_asset(context, asset); !ok) return std::unexpected(ok.error());
  std::error_code ec;
  if (!std::filesystem::is_regular_file(context.project_dir() / asset, ec)) {
    return std::unexpected(ToolError{"IO_ERROR", "prefab file not found: " + asset, asset, "/asset",
                                     "Create it with prefab.save"});
  }
  auto prefab = load_prefab_file(context.project_dir(), asset, context.registry());
  if (!prefab) return std::unexpected(ToolError{"SCHEMA_INVALID", prefab.error(), asset, "", ""});
  return std::move(*prefab);
}

bool is_uint(const Json& j) { return j.is_number_unsigned() || (j.is_number_integer() && j.get<long long>() >= 0); }

Node* instance_root_of(Node& node) {
  if (!node.prefab_link()) return nullptr;
  Node* root = &node;
  while (root->id() != root->prefab_link()->instance_id && root->parent() && root->parent()->prefab_link()) {
    root = root->parent();
  }
  return root->id() == node.prefab_link()->instance_id ? root : nullptr;
}

std::expected<Node*, ToolError> resolve_instance_root(ProjectContext& context, const std::string& selector) {
  auto node = resolve_node(context, selector);
  if (!node) return std::unexpected(node.error());
  Node* root = instance_root_of(**node);
  if (!root) {
    return std::unexpected(fail("NODE_NOT_FOUND", "node " + std::to_string((*node)->id()) + " is not part of a prefab instance",
                                context, "/node"));
  }
  return root;
}

Json override_to_json(const PrefabOverride& o, const ComponentRegistry& registry) {
  Json out = {{"local_id", o.local_id}, {"property", o.property}};
  if (!o.component.empty()) out["component"] = o.component;
  const auto type = prefab_override_type(registry, o.component, o.property);
  out["value"] = type ? property_to_json(*type, o.value) : Json(nullptr);
  return out;
}

Json overrides_to_json(const Node& root, const ComponentRegistry& registry) {
  Json list = Json::array();
  for (const PrefabOverride& o : root.prefab_link()->overrides) list.push_back(override_to_json(o, registry));
  return list;
}

std::expected<PrefabOverride, ToolError> override_from_json(ProjectContext& context, const Json& j,
                                                            const std::string& where) {
  if (!j.is_object() || !j.contains("local_id") || !is_uint(j["local_id"]) || !j.contains("property") ||
      !j["property"].is_string() || !j.contains("value") || (j.contains("component") && !j["component"].is_string())) {
    return std::unexpected(fail("ARGS_INVALID", "override needs unsigned 'local_id', string 'property', 'value' and an "
                                "optional string 'component'", context, where));
  }
  PrefabOverride o;
  o.local_id = j["local_id"].get<NodeId>();
  o.property = j["property"].get<std::string>();
  o.component = j.value("component", std::string());
  const auto type = prefab_override_type(context.registry(), o.component, o.property);
  if (!type) {
    return std::unexpected(fail("PROPERTY_UNKNOWN", "'" + o.component + "." + o.property + "' is not an overridable property",
                                context, where, "Node fields: name, position, rotation_radians, scale (no component)"));
  }
  auto value = property_from_json(*type, o.property, j["value"], context);
  if (!value) return std::unexpected(value.error());
  o.value = *value;
  return o;
}

std::expected<std::vector<PrefabOverride>, ToolError> overrides_from_json(ProjectContext& context, const Json& list,
                                                                           const std::string& where) {
  std::vector<PrefabOverride> out;
  for (std::size_t i = 0; i < list.size(); ++i) {
    auto o = override_from_json(context, list[i], where + "/" + std::to_string(i));
    if (!o) return std::unexpected(o.error());
    out.push_back(std::move(*o));
  }
  return out;
}

// Checks that an override names a real prefab node (and component).
std::expected<void, ToolError> check_against_prefab(ProjectContext& context, const Prefab& prefab,
                                                    const PrefabOverride& o, const std::string& where) {
  const Node* node = prefab.scene().find_node(o.local_id);
  if (!node) {
    return std::unexpected(fail("NODE_NOT_FOUND", "prefab has no node with local id " + std::to_string(o.local_id),
                                context, where));
  }
  if (!o.component.empty() && !node->find_component(o.component)) {
    return std::unexpected(fail("COMPONENT_NOT_FOUND", "prefab node " + std::to_string(o.local_id) + " has no '" +
                                o.component + "'", context, where));
  }
  return {};
}

void remove_subtree(Scene& scene, Node& node) {
  scene.destroy_node(node.id());
  scene.flush_destroyed();
}

std::size_t count_nodes(const Node& node) {
  std::size_t n = 1;
  for (const auto& child : node.children()) n += count_nodes(*child);
  return n;
}

void collect_instance(Node& node, NodeId instance_id, std::vector<Node*>& out) {
  if (node.prefab_link() && node.prefab_link()->instance_id == instance_id) out.push_back(&node);
  for (const auto& child : node.children()) collect_instance(*child, instance_id, out);
}

void collect_local_ids(const Node& node, Json& out) {
  out.push_back({{"local_id", node.prefab_link()->local_id}, {"id", node.id()}});
  for (const auto& child : node.children()) {
    if (child->prefab_link() && child->prefab_link()->instance_id == node.prefab_link()->instance_id) {
      collect_local_ids(*child, out);
    }
  }
}

const Json kOverrideListSchema = {{"type", "array"}};

}  // namespace

void register_prefab_commands(CommandRegistry& registry) {
  registry.add(
      {"prefab.save",
       "Write the subtree at 'node' to a prefab file (arcane.prefab v1). Local IDs are 1..N in depth-first order. "
       "The file is written immediately (not part of persist/dry_run); an existing file needs overwrite=true.",
       {{"type", "object"},
        {"properties", {{"node", {{"type", "string"}}}, {"asset", {{"type", "string"}}}, {"overwrite", {{"type", "boolean"}}}}},
        {"required", Json::array({"node", "asset"})},
        {"additionalProperties", false}},
       {{"type", "object"}, {"properties", {{"asset", {{"type", "string"}}}, {"nodes", {{"type", "integer"}}}}}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string asset = args["asset"].get<std::string>();
        if (auto ok = check_asset(context, asset); !ok) return std::unexpected(ok.error());
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        const std::filesystem::path path = context.project_dir() / asset;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !args.value("overwrite", false)) {
          return std::unexpected(ToolError{"CONFLICT", "prefab file already exists: " + asset, asset, "/asset",
                                           "Pass overwrite=true to replace it"});
        }
        auto prefab = Prefab::from_node(**node, context.registry());
        if (!prefab) return std::unexpected(fail("VALUE_INVALID", prefab.error(), context, "/node"));
        auto text = prefab->to_json(context.registry());
        if (!text) return std::unexpected(fail("VALUE_INVALID", text.error(), context, "/node"));
        std::filesystem::create_directories(path.parent_path(), ec);
        if (auto written = write_text_file_atomic(path, *text); !written) {
          return std::unexpected(ToolError{"IO_ERROR", written.error(), asset, "", ""});
        }
        return CommandResult{{{"asset", asset}, {"nodes", count_nodes(prefab->root())}}, std::nullopt};
      });

  registry.add(
      {"prefab.instantiate",
       "Create a copy of a prefab with fresh scene IDs under 'parent' (default: a scene root). 'name' and 'position' "
       "are shortcuts for overrides of the root; 'overrides' is a list of {local_id, component?, property, value}.",
       {{"type", "object"},
        {"properties",
         {{"asset", {{"type", "string"}}},
          {"parent", {{"type", "string"}}},
          {"name", {{"type", "string"}}},
          {"position", {{"type", "array"}}},
          {"overrides", kOverrideListSchema}}},
        {"required", Json::array({"asset"})},
        {"additionalProperties", false}},
       {{"type", "object"}, {"properties", {{"instance_id", {{"type", "integer"}}}, {"nodes", {{"type", "array"}}}}}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string asset = args["asset"].get<std::string>();
        auto prefab = load_prefab(context, asset);
        if (!prefab) return std::unexpected(prefab.error());
        Node* parent = nullptr;
        if (args.contains("parent")) {
          auto resolved = resolve_node(context, args["parent"].get<std::string>());
          if (!resolved) return std::unexpected(resolved.error());
          parent = *resolved;
        }
        std::vector<PrefabOverride> overrides;
        if (args.contains("overrides")) {
          auto list = overrides_from_json(context, args["overrides"], "/overrides");
          if (!list) return std::unexpected(list.error());
          overrides = std::move(*list);
        }
        const NodeId root_local = prefab->root().id();
        if (args.contains("name")) {
          overrides.push_back({root_local, "", "name", PropertyValue{args["name"].get<std::string>()}});
        }
        if (args.contains("position")) {
          auto value = property_from_json(PropertyType::Vec2, "position", args["position"], context);
          if (!value) return std::unexpected(value.error());
          overrides.push_back({root_local, "", "position", *value});
        }
        for (std::size_t i = 0; i < overrides.size(); ++i) {
          if (auto ok = check_against_prefab(context, *prefab, overrides[i], "/overrides/" + std::to_string(i)); !ok) {
            return std::unexpected(ok.error());
          }
        }

        auto root = instantiate_prefab(context.scene(), *prefab, asset, parent, context.registry());
        if (!root) return std::unexpected(fail("VALUE_INVALID", root.error(), context, "/asset"));
        for (const PrefabOverride& o : overrides) {
          if (auto set = set_prefab_override(**root, *prefab, o.local_id, o.component, o.property, o.value,
                                             context.registry());
              !set) {
            const std::string message = set.error();
            remove_subtree(context.scene(), **root);
            return std::unexpected(fail("VALUE_INVALID", message, context, "/overrides"));
          }
        }
        Json nodes = Json::array();
        collect_local_ids(**root, nodes);
        return CommandResult{{{"instance_id", (*root)->id()}, {"nodes", nodes}},
                             Invocation{"prefab.remove", {{"node", "id:" + std::to_string((*root)->id())}}}};
      });

  registry.add(
      {"prefab.remove",
       "Delete a whole prefab instance (any node of it selects it). Undo restores the scene as it was, with the same IDs.",
       {{"type", "object"},
        {"properties", {{"node", {{"type", "string"}}}}},
        {"required", Json::array({"node"})},
        {"additionalProperties", false}},
       {{"type", "object"}, {"properties", {{"removed", {{"type", "integer"}}}}}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto root = resolve_instance_root(context, args["node"].get<std::string>());
        if (!root) return std::unexpected(root.error());
        // Undo restores the whole scene text so node and component IDs come back unchanged.
        auto before = context.scene().save_canonical_json(context.registry());
        if (!before) return std::unexpected(fail("VALUE_INVALID", before.error(), context));
        const std::size_t removed = count_nodes(**root);
        remove_subtree(context.scene(), **root);
        return CommandResult{{{"removed", removed}}, Invocation{"scene.restore", {{"text", std::move(*before)}}}};
      });

  registry.add(
      {"prefab.override",
       "Set one property of a prefab instance node and record it as an override of the prefab value. Without "
       "'component' the property is a node field (name, position, rotation_radians, scale). Setting the prefab's own "
       "value drops the override.",
       {{"type", "object"},
        {"properties",
         {{"node", {{"type", "string"}}}, {"component", {{"type", "string"}}}, {"property", {{"type", "string"}}}, {"value", Json::object()}}},
        {"required", Json::array({"node", "property", "value"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Node* root = instance_root_of(**node);
        if (!root) {
          return std::unexpected(fail("NODE_NOT_FOUND", "node is not part of a prefab instance", context, "/node"));
        }
        Json entry = {{"local_id", (*node)->prefab_link()->local_id}, {"property", args["property"]}, {"value", args["value"]}};
        if (args.contains("component")) entry["component"] = args["component"];
        auto parsed = override_from_json(context, entry, "/value");
        if (!parsed) return std::unexpected(parsed.error());
        auto prefab = load_prefab(context, root->prefab_link()->asset);
        if (!prefab) return std::unexpected(prefab.error());
        if (auto ok = check_against_prefab(context, *prefab, *parsed, "/component"); !ok) return std::unexpected(ok.error());

        const Json before = overrides_to_json(*root, context.registry());
        std::optional<PrefabOverride> previous;
        for (const PrefabOverride& o : root->prefab_link()->overrides) {
          if (o.local_id == parsed->local_id && o.component == parsed->component && o.property == parsed->property) {
            previous = o;
          }
        }
        if (auto set = set_prefab_override(*root, *prefab, parsed->local_id, parsed->component, parsed->property,
                                           parsed->value, context.registry());
            !set) {
          return std::unexpected(fail("VALUE_INVALID", set.error(), context, "/value"));
        }
        bool overridden = false;
        for (const PrefabOverride& o : root->prefab_link()->overrides) {
          overridden = overridden || (o.local_id == parsed->local_id && o.component == parsed->component &&
                                      o.property == parsed->property);
        }
        const bool changed = overrides_to_json(*root, context.registry()) != before;
        Json result = {{"instance_id", root->id()}, {"local_id", parsed->local_id}, {"property", parsed->property},
                       {"value", entry["value"]}, {"overridden", overridden}, {"changed", changed}};
        if (!changed) return CommandResult{result, std::nullopt};

        Json common = {{"node", args["node"]}, {"property", args["property"]}};
        if (args.contains("component")) common["component"] = args["component"];
        Invocation inverse{"prefab.revert", common};
        if (previous) {
          inverse = Invocation{"prefab.override", common};
          inverse.args["value"] = override_to_json(*previous, context.registry())["value"];
        }
        return CommandResult{result, inverse};
      });

  registry.add(
      {"prefab.revert",
       "Drop an override: the property returns to the prefab's value. No-op when the property is not overridden.",
       {{"type", "object"},
        {"properties", {{"node", {{"type", "string"}}}, {"component", {{"type", "string"}}}, {"property", {{"type", "string"}}}}},
        {"required", Json::array({"node", "property"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Node* root = instance_root_of(**node);
        if (!root) {
          return std::unexpected(fail("NODE_NOT_FOUND", "node is not part of a prefab instance", context, "/node"));
        }
        const NodeId local_id = (*node)->prefab_link()->local_id;
        const std::string component = args.value("component", std::string());
        const std::string property = args["property"].get<std::string>();
        std::optional<PrefabOverride> previous;
        for (const PrefabOverride& o : root->prefab_link()->overrides) {
          if (o.local_id == local_id && o.component == component && o.property == property) previous = o;
        }
        Json result = {{"instance_id", root->id()}, {"local_id", local_id}, {"property", property},
                       {"reverted", previous.has_value()}};
        if (!previous) return CommandResult{result, std::nullopt};
        auto prefab = load_prefab(context, root->prefab_link()->asset);
        if (!prefab) return std::unexpected(prefab.error());
        if (auto done = revert_prefab_override(*root, *prefab, local_id, component, property, context.registry()); !done) {
          return std::unexpected(fail("VALUE_INVALID", done.error(), context, "/property"));
        }
        Json inverse_args = {{"node", args["node"]}, {"property", property},
                             {"value", override_to_json(*previous, context.registry())["value"]}};
        if (!component.empty()) inverse_args["component"] = component;
        return CommandResult{result, Invocation{"prefab.override", inverse_args}};
      });

  registry.add(
      {"prefab.break_link",
       "Turn a prefab instance into plain nodes: links and override records are removed, IDs and values stay.",
       {{"type", "object"},
        {"properties", {{"node", {{"type", "string"}}}}},
        {"required", Json::array({"node"})},
        {"additionalProperties", false}},
       {{"type", "object"}, {"properties", {{"instance_id", {{"type", "integer"}}}, {"nodes", {{"type", "integer"}}}}}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto root = resolve_instance_root(context, args["node"].get<std::string>());
        if (!root) return std::unexpected(root.error());
        const PrefabLink link = *(*root)->prefab_link();
        std::vector<Node*> members;
        collect_instance(**root, link.instance_id, members);
        Json nodes = Json::array();
        for (const Node* n : members) nodes.push_back({{"id", n->id()}, {"local_id", n->prefab_link()->local_id}});
        Json inverse_args = {{"asset", link.asset}, {"instance_id", link.instance_id}, {"nodes", nodes},
                             {"overrides", overrides_to_json(**root, context.registry())}};
        break_prefab_link(**root);
        return CommandResult{{{"instance_id", link.instance_id}, {"nodes", members.size()}},
                             Invocation{"prefab.relink", inverse_args}};
      });

  registry.add(
      {"prefab.relink",
       "Re-attach plain nodes to a prefab as one instance (the inverse of prefab.break_link). 'nodes' pairs scene IDs "
       "with prefab-local IDs; 'instance_id' is the root's scene ID.",
       {{"type", "object"},
        {"properties",
         {{"asset", {{"type", "string"}}}, {"instance_id", {{"type", "integer"}}}, {"nodes", {{"type", "array"}}}, {"overrides", kOverrideListSchema}}},
        {"required", Json::array({"asset", "instance_id", "nodes"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       true},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        const std::string asset = args["asset"].get<std::string>();
        auto prefab = load_prefab(context, asset);
        if (!prefab) return std::unexpected(prefab.error());
        const Json& instance = args["instance_id"];
        if (!is_uint(instance) || instance.get<NodeId>() == 0) {
          return std::unexpected(fail("ARGS_INVALID", "instance_id must be a positive integer", context, "/instance_id"));
        }
        const NodeId instance_id = instance.get<NodeId>();
        std::vector<std::pair<Node*, NodeId>> targets;
        bool has_root = false;
        for (std::size_t i = 0; i < args["nodes"].size(); ++i) {
          const Json& entry = args["nodes"][i];
          const std::string where = "/nodes/" + std::to_string(i);
          if (!entry.is_object() || !entry.contains("id") || !is_uint(entry["id"]) ||
              !entry.contains("local_id") || !is_uint(entry["local_id"])) {
            return std::unexpected(fail("ARGS_INVALID", "node entries need unsigned 'id' and 'local_id'", context, where));
          }
          Node* node = context.scene().find_node(entry["id"].get<NodeId>());
          if (!node) return std::unexpected(fail("NODE_NOT_FOUND", "no node with id " + entry["id"].dump(), context, where));
          if (node->prefab_link()) return std::unexpected(fail("CONFLICT", "node is already part of a prefab instance", context, where));
          const NodeId local = entry["local_id"].get<NodeId>();
          if (!prefab->scene().find_node(local)) {
            return std::unexpected(fail("NODE_NOT_FOUND", "prefab has no node with local id " + std::to_string(local), context, where));
          }
          for (const auto& [other, other_local] : targets) {
            if (other == node || other_local == local) {
              return std::unexpected(fail("ARGS_INVALID", "duplicate node or local id", context, where));
            }
          }
          has_root = has_root || node->id() == instance_id;
          targets.emplace_back(node, local);
        }
        if (!has_root) return std::unexpected(fail("ARGS_INVALID", "instance_id must be one of the listed nodes", context, "/instance_id"));
        std::vector<PrefabOverride> overrides;
        if (args.contains("overrides")) {
          auto list = overrides_from_json(context, args["overrides"], "/overrides");
          if (!list) return std::unexpected(list.error());
          overrides = std::move(*list);
          for (std::size_t i = 0; i < overrides.size(); ++i) {
            if (auto ok = check_against_prefab(context, *prefab, overrides[i], "/overrides/" + std::to_string(i)); !ok) {
              return std::unexpected(ok.error());
            }
          }
        }
        for (auto& [node, local] : targets) {
          const bool is_root = node->id() == instance_id;
          node->set_prefab_link(PrefabLink{asset, instance_id, local, is_root ? overrides : std::vector<PrefabOverride>{}});
        }
        return CommandResult{{{"instance_id", instance_id}, {"nodes", targets.size()}},
                             Invocation{"prefab.break_link", {{"node", "id:" + std::to_string(instance_id)}}}};
      });

  registry.add(
      {"prefab.info",
       "Report whether a node belongs to a prefab instance and list the instance's overrides.",
       {{"type", "object"},
        {"properties", {{"node", {{"type", "string"}}}}},
        {"required", Json::array({"node"})},
        {"additionalProperties", false}},
       {{"type", "object"}},
       false},
      [](ProjectContext& context, const Json& args) -> std::expected<CommandResult, ToolError> {
        auto node = resolve_node(context, args["node"].get<std::string>());
        if (!node) return std::unexpected(node.error());
        Node* root = instance_root_of(**node);
        if (!root) return CommandResult{{{"linked", false}}, std::nullopt};
        return CommandResult{{{"linked", true},
                              {"asset", root->prefab_link()->asset},
                              {"instance_id", root->id()},
                              {"local_id", (*node)->prefab_link()->local_id},
                              {"overrides", overrides_to_json(*root, context.registry())}},
                             std::nullopt};
      });
}

}  // namespace arcane::tooling
