// JSON (de)serialization for Scene. Writes canonical scene v2
// (docs/formats/scene.md); reads v2 strictly and legacy format_version 1
// leniently (docs/decisions.md). Unknown component type -> error.
#include <algorithm>
#include <format>
#include <unordered_set>

#include "arcane/canonical_json.hpp"
#include "arcane/component.hpp"
#include "arcane/migration.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"
#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;

constexpr const char* kSceneFormat = "arcane.scene";

json property_value_to_json(PropertyType type, const PropertyValue& value) {
  switch (type) {
    case PropertyType::Bool:
      return std::get<bool>(value);
    case PropertyType::Int:
      return std::get<int>(value);
    case PropertyType::Float:
      return std::get<float>(value);
    case PropertyType::Vec2: {
      const Vec2& v = std::get<Vec2>(value);
      return json::array({v.x, v.y});
    }
    case PropertyType::Color: {
      const Color& c = std::get<Color>(value);
      return json::array({c.r, c.g, c.b, c.a});
    }
    case PropertyType::String:
      return std::get<std::string>(value);
    case PropertyType::AssetId:
    case PropertyType::EntityId:
      return std::get<AssetId>(value);
  }
  return nullptr;
}

PropertyValue property_value_from_json(PropertyType type, const json& j, const std::string& context) {
  switch (type) {
    case PropertyType::Bool:
      return canonical::as_bool(j, context);
    case PropertyType::Int:
      return canonical::as_int(j, context);
    case PropertyType::Float:
      return canonical::as_float(j, context);
    case PropertyType::Vec2:
      return canonical::as_vec2(j, context);
    case PropertyType::Color:
      return canonical::as_color(j, context);
    case PropertyType::String:
      return canonical::as_string(j, context);
    case PropertyType::AssetId:
    case PropertyType::EntityId:
      return static_cast<AssetId>(canonical::as_uint(j, context));
  }
  canonical::fail(std::format("{}: unknown property type", context));
}

void serialize_node(const Node& node, const ComponentRegistry& registry, json& out_nodes) {
  json entry;
  entry["id"] = node.id();
  entry["name"] = node.name();
  entry["parent_id"] = node.parent() ? node.parent()->id() : kInvalidNodeId;
  const Transform2D& t = node.local_transform();
  entry["position"] = json::array({t.position.x, t.position.y});
  entry["rotation_radians"] = t.rotation_radians;
  entry["scale"] = json::array({t.scale.x, t.scale.y});

  json components = json::array();
  for (const auto& component : node.components()) {
    json c;
    c["id"] = component->id();
    c["type"] = component->type_name();
    json properties = json::object();
    if (const auto* descriptors = registry.properties_of(component->type_name())) {
      for (const PropertyDescriptor& descriptor : *descriptors) {
        properties[descriptor.name] = property_value_to_json(descriptor.type, descriptor.get(*component));
      }
    }
    c["properties"] = std::move(properties);
    components.push_back(std::move(c));
  }
  entry["components"] = std::move(components);
  out_nodes.push_back(std::move(entry));

  for (const auto& child : node.children()) {
    serialize_node(*child, registry, out_nodes);
  }
}

}  // namespace

std::expected<std::string, std::string> Scene::save_canonical_json(const ComponentRegistry& registry) const {
  json root;
  root["format"] = kSceneFormat;
  root["version"] = 2;
  root["next_id"] = next_id_;
  json nodes = json::array();
  for (const auto& r : roots_) {
    serialize_node(*r, registry, nodes);
  }
  root["nodes"] = std::move(nodes);
  return canonical::dump(root);
}

std::string Scene::save_to_json(const ComponentRegistry& registry) const {
  return save_canonical_json(registry).value_or(std::string());
}

std::expected<Scene, std::string> Scene::load_from_json(const std::string& text, const ComponentRegistry& registry) {
  return load_scene(text, registry, false);
}

std::expected<Scene, std::string> Scene::load_from_json_strict(const std::string& text,
                                                                const ComponentRegistry& registry) {
  return load_scene(text, registry, true);
}

std::expected<Scene, std::string> Scene::load_scene(const std::string& text, const ComponentRegistry& registry,
                                                     bool strict_unknown) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) {
    return std::unexpected(std::format("malformed scene JSON: {}", parsed.error()));
  }
  try {
    const json& root = *parsed;
    const canonical::Dialect dialect = canonical::detect_dialect(root, kSceneFormat, "scene");
    // v2 files are always checked strictly; legacy files only when migrating.
    const bool strict = strict_unknown || dialect == canonical::Dialect::V2;
    if (strict) {
      if (dialect == canonical::Dialect::V2) {
        canonical::require_only_keys(root, {"format", "version", "next_id", "nodes"}, "scene");
      } else {
        canonical::require_only_keys(root, {"format_version", "next_id", "nodes"}, "scene");
      }
    }
    if (!root.contains("nodes") || !root["nodes"].is_array()) {
      return std::unexpected(std::string("scene JSON is missing a 'nodes' array"));
    }

    Scene scene;
    NodeId max_id = 0;
    std::unordered_set<std::uint64_t> used_ids;  // nodes and components share one id counter

    // First pass: create every node as a root; parent/child edges are
    // reattached in the second pass so node order in the file doesn't matter.
    for (const auto& entry : root["nodes"]) {
      canonical::require_object(entry, "scene node");
      if (!entry.contains("id") || !entry.contains("name")) {
        return std::unexpected(std::string("scene node entry is missing 'id' or 'name'"));
      }
      const NodeId id = canonical::as_uint(entry["id"], "node 'id'");
      if (id == kInvalidNodeId) {
        return std::unexpected(std::string("scene node has invalid id 0"));
      }
      if (!used_ids.insert(id).second) {
        return std::unexpected(std::format("duplicate node id {} in scene JSON", id));
      }
      const std::string where = std::format("node {}", id);
      if (strict) {
        canonical::require_only_keys(
            entry, {"id", "name", "parent_id", "position", "rotation_radians", "scale", "components"}, where);
      }
      Node* node = scene.create_node_with_id(id, canonical::as_string(entry["name"], where + " 'name'"), nullptr);
      max_id = std::max(max_id, id);

      Transform2D transform;
      if (entry.contains("position")) transform.position = canonical::as_vec2(entry["position"], where + " 'position'");
      if (entry.contains("rotation_radians")) {
        transform.rotation_radians = canonical::as_float(entry["rotation_radians"], where + " 'rotation_radians'");
      }
      if (entry.contains("scale")) transform.scale = canonical::as_vec2(entry["scale"], where + " 'scale'");
      node->local_transform() = transform;

      if (!entry.contains("components")) continue;
      if (!entry["components"].is_array()) {
        return std::unexpected(std::format("{}: 'components' must be an array", where));
      }
      for (const auto& c : entry["components"]) {
        canonical::require_object(c, where + " component");
        if (!c.contains("id") || !c.contains("type")) {
          return std::unexpected(std::string("scene component entry is missing 'id' or 'type'"));
        }
        const std::string type_name = canonical::as_string(c["type"], where + " component 'type'");
        if (!registry.contains(type_name)) {
          return std::unexpected(std::format("unknown component type '{}' referenced by node '{}'", type_name,
                                              node->name()));
        }
        const ComponentId component_id = canonical::as_uint(c["id"], where + " component 'id'");
        if (component_id == 0 || !used_ids.insert(component_id).second) {
          return std::unexpected(std::format("{}: component id {} is zero or already used", where, component_id));
        }
        const std::string cwhere = std::format("{} component {} ({})", where, component_id, type_name);
        if (strict) canonical::require_only_keys(c, {"id", "type", "properties"}, cwhere);
        Component* component = scene.add_component_with_id(node, type_name, component_id, registry);
        max_id = std::max(max_id, component_id);

        if (!c.contains("properties")) continue;
        canonical::require_object(c["properties"], cwhere + " 'properties'");
        const auto* descriptors = registry.properties_of(type_name);
        for (auto it = c["properties"].begin(); it != c["properties"].end(); ++it) {
          const PropertyDescriptor* descriptor = nullptr;
          if (descriptors) {
            for (const PropertyDescriptor& candidate : *descriptors) {
              if (candidate.name == it.key()) {
                descriptor = &candidate;
                break;
              }
            }
          }
          if (!descriptor) {
            if (strict) {
              return std::unexpected(std::format("{}: unknown property '{}'", cwhere, it.key()));
            }
            continue;  // legacy leniency: unknown property on a known type is ignored, see docs/decisions.md
          }
          descriptor->set(*component, property_value_from_json(descriptor->type, it.value(),
                                                               std::format("{} property '{}'", cwhere, it.key())));
        }
      }
    }

    // Second pass: reattach each node to its declared parent now that every
    // id exists.
    for (const auto& entry : root["nodes"]) {
      const NodeId id = entry["id"].get<NodeId>();
      const NodeId parent_id =
          entry.contains("parent_id") ? canonical::as_uint(entry["parent_id"], "node 'parent_id'") : kInvalidNodeId;
      if (parent_id == kInvalidNodeId) {
        continue;
      }
      Node* parent = scene.find_node(parent_id);
      if (!parent) {
        return std::unexpected(std::format("node {} references unknown parent_id {}", id, parent_id));
      }
      if (!scene.reparent(scene.find_node(id), parent)) {
        return std::unexpected(std::format("node {} has a cyclic or invalid parent_id {}", id, parent_id));
      }
    }

    const NodeId declared_next_id =
        root.contains("next_id") ? canonical::as_uint(root["next_id"], "scene 'next_id'") : NodeId{0};
    scene.set_next_id(std::max<NodeId>({declared_next_id, max_id + 1, 1}));
    return scene;
  } catch (const canonical::SchemaError& ex) {
    return std::unexpected(std::string(ex.what()));
  }
}

std::expected<std::string, std::string> migrate_scene_json(const std::string& text, const ComponentRegistry& registry) {
  auto scene = Scene::load_from_json_strict(text, registry);
  if (!scene) {
    return std::unexpected(scene.error());
  }
  auto migrated = scene->save_canonical_json(registry);
  if (!migrated) {
    return std::unexpected(migrated.error());
  }
  // The result must be a fixed point: reload and re-save gives the same bytes.
  auto reloaded = Scene::load_from_json_strict(*migrated, registry);
  if (!reloaded) {
    return std::unexpected("migrated scene does not reload: " + reloaded.error());
  }
  auto again = reloaded->save_canonical_json(registry);
  if (!again || *again != *migrated) {
    return std::unexpected(std::string("migrated scene is not idempotent"));
  }
  return migrated;
}

}  // namespace arcane
