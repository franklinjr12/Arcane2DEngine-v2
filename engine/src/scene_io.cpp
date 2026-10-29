// JSON (de)serialization for Scene. Writes canonical scene v2
// (docs/formats/scene.md); reads v2 strictly and legacy format_version 1
// leniently (docs/decisions.md). Unknown component type -> error.
#include <algorithm>
#include <format>
#include <unordered_set>

#include "arcane/canonical_json.hpp"
#include "arcane/component.hpp"
#include "arcane/migration.hpp"
#include "arcane/prefab.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
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

// Tilemap::layers is a String property (";"/"," text, kept for the inspector
// and legacy files); scene v2 stores it structurally: layers -> rows -> ids.
bool is_tile_layers(const Component& component, const PropertyDescriptor& descriptor) {
  return descriptor.name == "layers" && component.type_name() == "Tilemap";
}

json tile_layers_to_json(const Tilemap& map) {
  json layers = json::array();
  for (int layer = 0; layer < map.layer_count(); ++layer) {
    json rows = json::array();
    for (int y = 0; y < map.height; ++y) {
      json row = json::array();
      for (int x = 0; x < map.width; ++x) row.push_back(map.tile_at(layer, x, y));
      rows.push_back(std::move(row));
    }
    layers.push_back(std::move(rows));
  }
  return layers;
}

// Fills `map` (whose width/height are already set) from a structured array
// or, strictly, from the legacy text form. Cells outside width x height are
// errors so no tile data is dropped.
void set_tile_layers(Tilemap& map, const json& value, bool strict, const std::string& context) {
  std::vector<std::vector<int>> layers;  // row-major, width*height or fewer ids per layer
  const auto cells = static_cast<std::size_t>(std::max(map.width, 0)) * static_cast<std::size_t>(std::max(map.height, 0));
  if (value.is_string()) {
    const std::string text = value.get<std::string>();
    if (!strict) {
      map.set_layers_from_string(text);
      return;
    }
    std::size_t start = 0;
    while (!text.empty() && start <= text.size()) {
      std::size_t end = text.find(';', start);
      if (end == std::string::npos) end = text.size();
      std::vector<int> ids;
      std::size_t pos = start;
      while (pos < end) {
        std::size_t comma = text.find(',', pos);
        if (comma == std::string::npos || comma > end) comma = end;
        const std::string token = text.substr(pos, comma - pos);
        if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos || token.size() > 9) {
          canonical::fail(std::format("{}: bad tile id '{}'", context, token));
        }
        ids.push_back(std::stoi(token));
        pos = comma + 1;
      }
      layers.push_back(std::move(ids));
      start = end + 1;
    }
  } else {
    if (!value.is_array()) canonical::fail(std::format("{}: expected an array of layers", context));
    for (std::size_t l = 0; l < value.size(); ++l) {
      const std::string where = std::format("{} layer {}", context, l);
      const json& rows = value[l];
      if (!rows.is_array() || rows.size() != static_cast<std::size_t>(std::max(map.height, 0))) {
        canonical::fail(std::format("{}: expected {} rows (the map height)", where, map.height));
      }
      std::vector<int> ids;
      for (std::size_t y = 0; y < rows.size(); ++y) {
        const json& row = rows[y];
        if (!row.is_array() || row.size() != static_cast<std::size_t>(std::max(map.width, 0))) {
          canonical::fail(std::format("{} row {}: expected {} tile ids (the map width)", where, y, map.width));
        }
        for (const json& id : row) {
          const int tile = canonical::as_int(id, where);
          if (tile < 0) canonical::fail(std::format("{} row {}: negative tile id", where, y));
          ids.push_back(tile);
        }
      }
      layers.push_back(std::move(ids));
    }
  }
  map.set_layers_from_string("");
  for (std::size_t l = 0; l < layers.size(); ++l) {
    if (layers[l].size() > cells) {
      canonical::fail(std::format("{} layer {}: {} cells do not fit the {}x{} map", context, l, layers[l].size(),
                                  map.width, map.height));
    }
    map.add_layer();
    for (std::size_t i = 0; i < layers[l].size(); ++i) {
      if (layers[l][i] == 0) continue;
      const int w = std::max(map.width, 1);
      map.set_tile(static_cast<int>(l), static_cast<int>(i) % w, static_cast<int>(i) / w, layers[l][i]);
    }
  }
}

json prefab_link_to_json(const PrefabLink& link, const ComponentRegistry& registry) {
  json out;
  out["asset"] = link.asset;
  out["instance_id"] = link.instance_id;
  out["local_id"] = link.local_id;
  if (!link.overrides.empty()) {
    json overrides = json::array();
    for (const PrefabOverride& o : link.overrides) {
      json entry;
      entry["local_id"] = o.local_id;
      if (!o.component.empty()) entry["component"] = o.component;
      entry["property"] = o.property;
      const auto type = prefab_override_type(registry, o.component, o.property);
      entry["value"] = type ? property_value_to_json(*type, o.value) : json(nullptr);
      overrides.push_back(std::move(entry));
    }
    out["overrides"] = std::move(overrides);
  }
  return out;
}

PrefabLink prefab_link_from_json(const json& j, const ComponentRegistry& registry, const std::string& where) {
  canonical::require_object(j, where + " 'prefab'");
  canonical::require_only_keys(j, {"asset", "instance_id", "local_id", "overrides"}, where + " 'prefab'");
  for (const char* key : {"asset", "instance_id", "local_id"}) {
    if (!j.contains(key)) canonical::fail(std::format("{} 'prefab': missing '{}'", where, key));
  }
  PrefabLink link;
  link.asset = canonical::as_string(j["asset"], where + " prefab 'asset'");
  link.instance_id = canonical::as_uint(j["instance_id"], where + " prefab 'instance_id'");
  link.local_id = canonical::as_uint(j["local_id"], where + " prefab 'local_id'");
  canonical::require_project_path(link.asset, false, where + " prefab 'asset'");
  if (link.instance_id == 0 || link.local_id == 0) {
    canonical::fail(std::format("{} 'prefab': instance_id and local_id must be set", where));
  }
  if (!j.contains("overrides")) return link;
  if (!j["overrides"].is_array()) canonical::fail(where + " prefab 'overrides' must be an array");
  for (const json& o : j["overrides"]) {
    const std::string owhere = where + " prefab override";
    canonical::require_object(o, owhere);
    canonical::require_only_keys(o, {"local_id", "component", "property", "value"}, owhere);
    if (!o.contains("local_id") || !o.contains("property") || !o.contains("value")) {
      canonical::fail(owhere + ": needs 'local_id', 'property' and 'value'");
    }
    PrefabOverride entry;
    entry.local_id = canonical::as_uint(o["local_id"], owhere + " 'local_id'");
    if (o.contains("component")) entry.component = canonical::as_string(o["component"], owhere + " 'component'");
    entry.property = canonical::as_string(o["property"], owhere + " 'property'");
    const auto type = prefab_override_type(registry, entry.component, entry.property);
    if (!type) canonical::fail(std::format("{}: '{}.{}' is not an overridable property", owhere, entry.component, entry.property));
    entry.value = property_value_from_json(*type, o["value"], owhere + " 'value'");
    link.overrides.push_back(std::move(entry));
  }
  return link;
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
        properties[descriptor.name] =
            is_tile_layers(*component, descriptor)
                ? tile_layers_to_json(static_cast<const Tilemap&>(*component))
                : property_value_to_json(descriptor.type, descriptor.get(*component));
      }
    }
    c["properties"] = std::move(properties);
    components.push_back(std::move(c));
  }
  entry["components"] = std::move(components);
  if (node.prefab_link()) entry["prefab"] = prefab_link_to_json(*node.prefab_link(), registry);
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
            entry, {"id", "name", "parent_id", "position", "rotation_radians", "scale", "components", "prefab"}, where);
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
      if (entry.contains("prefab")) node->set_prefab_link(prefab_link_from_json(entry["prefab"], registry, where));

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
        const json* tile_layers = nullptr;  // applied after width/height, whatever the key order
        const PropertyDescriptor* tile_layers_descriptor = nullptr;
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
          if (is_tile_layers(*component, *descriptor)) {
            tile_layers = &it.value();
            tile_layers_descriptor = descriptor;
            continue;
          }
          descriptor->set(*component, property_value_from_json(descriptor->type, it.value(),
                                                               std::format("{} property '{}'", cwhere, it.key())));
        }
        if (tile_layers) {
          set_tile_layers(static_cast<Tilemap&>(*component), *tile_layers, strict,
                          std::format("{} property '{}'", cwhere, tile_layers_descriptor->name));
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

    // A link must point at a real instance root that carries the same asset.
    for (const auto& entry : root["nodes"]) {
      const Node* node = scene.find_node(entry["id"].get<NodeId>());
      if (!node->prefab_link()) continue;
      const PrefabLink& link = *node->prefab_link();
      const Node* instance = scene.find_node(link.instance_id);
      if (!instance || !instance->prefab_link() || instance->prefab_link()->instance_id != link.instance_id ||
          instance->prefab_link()->asset != link.asset) {
        return std::unexpected(std::format("node {} has a prefab link to a missing instance root {}", node->id(),
                                           link.instance_id));
      }
      if (!link.overrides.empty() && node != instance) {
        return std::unexpected(std::format("node {}: only an instance root can hold prefab overrides", node->id()));
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
