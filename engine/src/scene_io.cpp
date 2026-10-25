// JSON (de)serialization for Scene: see docs/decisions.md for the schema
// (format_version 1) and the "unknown component type -> error" choice.
#include <json.hpp>

#include <algorithm>
#include <format>

#include "arcane/component.hpp"
#include "arcane/property.hpp"
#include "arcane/scene.hpp"

namespace arcane {

namespace {

constexpr int kSceneFormatVersion = 1;

nlohmann::json property_value_to_json(PropertyType type, const PropertyValue& value) {
  switch (type) {
    case PropertyType::Bool:
      return std::get<bool>(value);
    case PropertyType::Int:
      return std::get<int>(value);
    case PropertyType::Float:
      return std::get<float>(value);
    case PropertyType::Vec2: {
      const Vec2& v = std::get<Vec2>(value);
      return nlohmann::json::array({v.x, v.y});
    }
    case PropertyType::Color: {
      const Color& c = std::get<Color>(value);
      return nlohmann::json::array({c.r, c.g, c.b, c.a});
    }
    case PropertyType::String:
      return std::get<std::string>(value);
    case PropertyType::AssetId:
    case PropertyType::EntityId:
      return std::get<AssetId>(value);
  }
  return nullptr;
}

std::expected<PropertyValue, std::string> property_value_from_json(PropertyType type, const std::string& name,
                                                                     const nlohmann::json& j) {
  try {
    switch (type) {
      case PropertyType::Bool:
        return PropertyValue{j.get<bool>()};
      case PropertyType::Int:
        return PropertyValue{j.get<int>()};
      case PropertyType::Float:
        return PropertyValue{j.get<float>()};
      case PropertyType::Vec2:
        return PropertyValue{Vec2{j.at(0).get<float>(), j.at(1).get<float>()}};
      case PropertyType::Color:
        return PropertyValue{Color{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(),
                                    j.at(3).get<float>()}};
      case PropertyType::String:
        return PropertyValue{j.get<std::string>()};
      case PropertyType::AssetId:
      case PropertyType::EntityId:
        return PropertyValue{j.get<AssetId>()};
    }
  } catch (const nlohmann::json::exception& ex) {
    return std::unexpected(std::format("property '{}': {}", name, ex.what()));
  }
  return std::unexpected(std::format("property '{}': unknown type", name));
}

void serialize_node(const Node& node, const ComponentRegistry& registry, nlohmann::json& out_nodes) {
  nlohmann::json entry;
  entry["id"] = node.id();
  entry["name"] = node.name();
  entry["parent_id"] = node.parent() ? node.parent()->id() : kInvalidNodeId;
  const Transform2D& t = node.local_transform();
  entry["position"] = nlohmann::json::array({t.position.x, t.position.y});
  entry["rotation_radians"] = t.rotation_radians;
  entry["scale"] = nlohmann::json::array({t.scale.x, t.scale.y});

  nlohmann::json components = nlohmann::json::array();
  for (const auto& component : node.components()) {
    nlohmann::json c;
    c["id"] = component->id();
    c["type"] = component->type_name();
    nlohmann::json properties = nlohmann::json::object();
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

std::string Scene::save_to_json(const ComponentRegistry& registry) const {
  nlohmann::json root;
  root["format_version"] = kSceneFormatVersion;
  root["next_id"] = next_id_;
  nlohmann::json nodes = nlohmann::json::array();
  for (const auto& r : roots_) {
    serialize_node(*r, registry, nodes);
  }
  root["nodes"] = std::move(nodes);
  return root.dump(2);
}

std::expected<Scene, std::string> Scene::load_from_json(const std::string& text, const ComponentRegistry& registry) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error& ex) {
    return std::unexpected(std::format("malformed scene JSON: {}", ex.what()));
  }

  if (!root.contains("format_version") || !root["format_version"].is_number_integer()) {
    return std::unexpected(std::string("scene JSON is missing a numeric 'format_version' field"));
  }
  const int file_version = root["format_version"].get<int>();
  if (file_version != kSceneFormatVersion) {
    return std::unexpected(std::format(
        "unsupported scene format_version {} (this build only supports version {}; {} than supported)", file_version,
        kSceneFormatVersion, file_version > kSceneFormatVersion ? "newer" : "older"));
  }
  if (!root.contains("nodes") || !root["nodes"].is_array()) {
    return std::unexpected(std::string("scene JSON is missing a 'nodes' array"));
  }

  Scene scene;
  NodeId max_id = 0;

  // First pass: create every node as a root; parent/child edges are
  // reattached in the second pass so node order in the file doesn't matter.
  for (const auto& entry : root["nodes"]) {
    if (!entry.contains("id") || !entry.contains("name")) {
      return std::unexpected(std::string("scene node entry is missing 'id' or 'name'"));
    }
    const NodeId id = entry["id"].get<NodeId>();
    if (id == kInvalidNodeId) {
      return std::unexpected(std::string("scene node has invalid id 0"));
    }
    if (scene.find_node(id) != nullptr) {
      return std::unexpected(std::format("duplicate node id {} in scene JSON", id));
    }
    Node* node = scene.create_node_with_id(id, entry["name"].get<std::string>(), nullptr);
    max_id = std::max(max_id, id);

    if (entry.contains("position") || entry.contains("rotation_radians") || entry.contains("scale")) {
      Transform2D transform;
      if (entry.contains("position")) {
        transform.position = Vec2{entry["position"].at(0).get<float>(), entry["position"].at(1).get<float>()};
      }
      if (entry.contains("rotation_radians")) {
        transform.rotation_radians = entry["rotation_radians"].get<float>();
      }
      if (entry.contains("scale")) {
        transform.scale = Vec2{entry["scale"].at(0).get<float>(), entry["scale"].at(1).get<float>()};
      }
      node->local_transform() = transform;
    }

    if (entry.contains("components")) {
      for (const auto& c : entry["components"]) {
        if (!c.contains("id") || !c.contains("type")) {
          return std::unexpected(std::string("scene component entry is missing 'id' or 'type'"));
        }
        const std::string type_name = c["type"].get<std::string>();
        if (!registry.contains(type_name)) {
          return std::unexpected(std::format("unknown component type '{}' referenced by node '{}'", type_name,
                                              node->name()));
        }
        const ComponentId component_id = c["id"].get<ComponentId>();
        Component* component = scene.add_component_with_id(node, type_name, component_id, registry);
        max_id = std::max(max_id, component_id);

        if (c.contains("properties")) {
          const auto* descriptors = registry.properties_of(type_name);
          for (auto it = c["properties"].begin(); it != c["properties"].end(); ++it) {
            const std::string& prop_name = it.key();
            const PropertyDescriptor* descriptor = nullptr;
            if (descriptors) {
              for (const PropertyDescriptor& candidate : *descriptors) {
                if (candidate.name == prop_name) {
                  descriptor = &candidate;
                  break;
                }
              }
            }
            if (!descriptor) {
              continue;  // unknown property on an otherwise-known type: ignore, see docs/decisions.md
            }
            auto value = property_value_from_json(descriptor->type, prop_name, it.value());
            if (!value) {
              return std::unexpected(value.error());
            }
            descriptor->set(*component, *value);
          }
        }
      }
    }
  }

  // Second pass: reattach each node to its declared parent now that every
  // id exists.
  for (const auto& entry : root["nodes"]) {
    const NodeId id = entry["id"].get<NodeId>();
    const NodeId parent_id = entry.value("parent_id", kInvalidNodeId);
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

  const NodeId declared_next_id = root.value("next_id", NodeId{0});
  scene.set_next_id(std::max<NodeId>({declared_next_id, max_id + 1, 1}));
  return scene;
}

}  // namespace arcane
