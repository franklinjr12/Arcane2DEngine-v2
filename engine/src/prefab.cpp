// Prefab capture, (de)serialization, instancing and override bookkeeping.
// The file format reuses the scene node list (docs/formats/prefab.md), so
// parsing and writing go through Scene with a swapped header.
#include "arcane/prefab.hpp"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <variant>

#include "canonical_json.hpp"

namespace arcane {

namespace {

using nlohmann::json;
using IdMap = std::unordered_map<std::uint64_t, std::uint64_t>;

constexpr const char* kPrefabFormat = "arcane.prefab";
constexpr int kPrefabVersion = 1;

Node* clone_subtree(const Node& src, Scene& dst, Node* parent, const ComponentRegistry& registry, IdMap& ids,
                    std::vector<Component*>& made) {
  Node* node = dst.create_node(src.name(), parent);
  ids[src.id()] = node->id();
  node->local_transform() = src.local_transform();
  for (const auto& component : src.components()) {
    Component* copy = dst.add_component(node, component->type_name(), registry);
    if (!copy) continue;
    ids[component->id()] = copy->id();
    if (const auto* descriptors = registry.properties_of(component->type_name())) {
      for (const PropertyDescriptor& d : *descriptors) d.set(*copy, d.get(*component));
    }
    made.push_back(copy);
  }
  for (const auto& child : src.children()) clone_subtree(*child, dst, node, registry, ids, made);
  return node;
}

// Node references between copied nodes follow the copy; anything else is cleared.
void remap_entity_ids(const std::vector<Component*>& made, const IdMap& ids, const ComponentRegistry& registry) {
  for (Component* component : made) {
    const auto* descriptors = registry.properties_of(component->type_name());
    if (!descriptors) continue;
    for (const PropertyDescriptor& d : *descriptors) {
      if (d.type != PropertyType::EntityId) continue;
      const auto old_id = std::get<AssetId>(d.get(*component));
      if (old_id == 0) continue;
      const auto it = ids.find(old_id);
      d.set(*component, PropertyValue{static_cast<AssetId>(it == ids.end() ? 0 : it->second)});
    }
  }
}

bool type_matches(PropertyType type, const PropertyValue& value) {
  switch (type) {
    case PropertyType::Bool: return std::holds_alternative<bool>(value);
    case PropertyType::Int: return std::holds_alternative<int>(value);
    case PropertyType::Float: return std::holds_alternative<float>(value);
    case PropertyType::Vec2: return std::holds_alternative<Vec2>(value);
    case PropertyType::Color: return std::holds_alternative<Color>(value);
    case PropertyType::String: return std::holds_alternative<std::string>(value);
    case PropertyType::AssetId:
    case PropertyType::EntityId: return std::holds_alternative<AssetId>(value);
  }
  return false;
}

bool same_value(const PropertyValue& a, const PropertyValue& b) {
  if (a.index() != b.index()) return false;
  if (const auto* ca = std::get_if<Color>(&a)) {
    const Color& cb = std::get<Color>(b);
    return ca->r == cb.r && ca->g == cb.g && ca->b == cb.b && ca->a == cb.a;
  }
  return a == b;
}

const PropertyDescriptor* find_descriptor(const ComponentRegistry& registry, const std::string& component,
                                          const std::string& property) {
  const auto* descriptors = registry.properties_of(component);
  if (!descriptors) return nullptr;
  for (const PropertyDescriptor& d : *descriptors) {
    if (d.name == property) return &d;
  }
  return nullptr;
}

std::expected<PropertyValue, std::string> read_value(const Node& node, const std::string& component,
                                                     const std::string& property, const ComponentRegistry& registry) {
  if (component.empty()) {
    const Transform2D& t = node.local_transform();
    if (property == "name") return PropertyValue{node.name()};
    if (property == "position") return PropertyValue{t.position};
    if (property == "rotation_radians") return PropertyValue{t.rotation_radians};
    return PropertyValue{t.scale};
  }
  const Component* c = node.find_component(component);
  const PropertyDescriptor* d = find_descriptor(registry, component, property);
  if (!c || !d) return std::unexpected(std::format("node {} has no {}.{}", node.id(), component, property));
  return d->get(*c);
}

void write_value(Node& node, const std::string& component, const std::string& property, const PropertyValue& value,
                 const ComponentRegistry& registry) {
  if (component.empty()) {
    Transform2D& t = node.local_transform();
    if (property == "name") node.set_name(std::get<std::string>(value));
    else if (property == "position") t.position = std::get<Vec2>(value);
    else if (property == "rotation_radians") t.rotation_radians = std::get<float>(value);
    else t.scale = std::get<Vec2>(value);
    return;
  }
  find_descriptor(registry, component, property)->set(*node.find_component(component), value);
}

Node* find_instance_node(Node& root, NodeId instance_id, NodeId local_id) {
  const auto& link = root.prefab_link();
  if (link && link->instance_id == instance_id && link->local_id == local_id) return &root;
  for (const auto& child : root.children()) {
    if (Node* found = find_instance_node(*child, instance_id, local_id)) return found;
  }
  return nullptr;
}

void unlink_subtree(Node& node, NodeId instance_id) {
  const auto& link = node.prefab_link();
  if (link && link->instance_id == instance_id) node.set_prefab_link(std::nullopt);
  for (const auto& child : node.children()) unlink_subtree(*child, instance_id);
}

bool same_key(const PrefabOverride& o, NodeId local_id, const std::string& component, const std::string& property) {
  return o.local_id == local_id && o.component == component && o.property == property;
}

}  // namespace

std::optional<PropertyType> prefab_override_type(const ComponentRegistry& registry, const std::string& component,
                                                 const std::string& property) {
  if (component.empty()) {
    if (property == "name") return PropertyType::String;
    if (property == "position" || property == "scale") return PropertyType::Vec2;
    if (property == "rotation_radians") return PropertyType::Float;
    return std::nullopt;
  }
  if (component == "Tilemap" && property == "layers") return std::nullopt;  // tile data is not overridable
  const PropertyDescriptor* d = find_descriptor(registry, component, property);
  if (!d) return std::nullopt;
  return d->type;
}

std::expected<Prefab, std::string> Prefab::from_node(const Node& root, const ComponentRegistry& registry) {
  Scene scene;
  IdMap ids;
  std::vector<Component*> made;
  clone_subtree(root, scene, nullptr, registry, ids, made);
  remap_entity_ids(made, ids, registry);
  return Prefab(std::move(scene));
}

std::expected<Prefab, std::string> Prefab::from_json(const std::string& text, const ComponentRegistry& registry) {
  auto parsed = canonical::parse_strict(text);
  if (!parsed) return std::unexpected(std::format("malformed prefab JSON: {}", parsed.error()));
  json root = std::move(*parsed);
  if (!root.is_object() || root.value("format", std::string()) != kPrefabFormat) {
    return std::unexpected(std::string("not an arcane.prefab file"));
  }
  if (!root.contains("version") || !root["version"].is_number_integer() ||
      root["version"].get<int>() != kPrefabVersion) {
    return std::unexpected(std::string("unsupported prefab version (expected 1)"));
  }
  root["format"] = "arcane.scene";
  root["version"] = 2;
  auto scene_text = canonical::dump(root);
  if (!scene_text) return std::unexpected(scene_text.error());
  auto scene = Scene::load_from_json(*scene_text, registry);
  if (!scene) return std::unexpected(std::format("prefab: {}", scene.error()));
  if (scene->roots().size() != 1) return std::unexpected(std::string("prefab must have exactly one root node"));
  for (const json& node : root["nodes"]) {
    if (node.contains("prefab")) return std::unexpected(std::string("prefab nodes cannot be prefab instances"));
  }
  return Prefab(std::move(*scene));
}

std::expected<std::string, std::string> Prefab::to_json(const ComponentRegistry& registry) const {
  auto scene_text = scene_.save_canonical_json(registry);
  if (!scene_text) return scene_text;
  auto parsed = canonical::parse_strict(*scene_text);
  if (!parsed) return std::unexpected(parsed.error());
  (*parsed)["format"] = kPrefabFormat;
  (*parsed)["version"] = kPrefabVersion;
  return canonical::dump(*parsed);
}

std::expected<Node*, std::string> instantiate_prefab(Scene& scene, const Prefab& prefab, const std::string& asset,
                                                     Node* parent, const ComponentRegistry& registry) {
  if (asset.empty()) return std::unexpected(std::string("prefab asset path is empty"));
  if (parent && scene.find_node(parent->id()) != parent) {
    return std::unexpected(std::string("parent node does not belong to this scene"));
  }
  // clone_subtree maps prefab IDs to the new scene IDs for both nodes and components.
  IdMap ids;
  std::vector<Component*> made;
  Node* root = clone_subtree(prefab.root(), scene, parent, registry, ids, made);
  remap_entity_ids(made, ids, registry);

  // Link each new node back to its prefab-local ID.
  IdMap node_to_local;
  for (const auto& [local, created] : ids) node_to_local[created] = local;
  const auto link = [&](auto&& self, Node& node) -> void {
    node.set_prefab_link(PrefabLink{asset, root->id(), node_to_local.at(node.id()), {}});
    for (const auto& child : node.children()) self(self, *child);
  };
  link(link, *root);
  return root;
}

std::expected<void, std::string> set_prefab_override(Node& instance_root, const Prefab& prefab, NodeId local_id,
                                                     const std::string& component, const std::string& property,
                                                     const PropertyValue& value, const ComponentRegistry& registry) {
  if (!instance_root.prefab_link() || instance_root.prefab_link()->instance_id != instance_root.id()) {
    return std::unexpected(std::string("node is not a prefab instance root"));
  }
  const auto type = prefab_override_type(registry, component, property);
  if (!type) return std::unexpected(std::format("'{}.{}' is not an overridable property", component, property));
  if (!type_matches(*type, value)) {
    return std::unexpected(std::format("wrong value type for '{}.{}'", component, property));
  }
  Node* target = find_instance_node(instance_root, instance_root.id(), local_id);
  const Node* source = prefab.scene().find_node(local_id);
  if (!target || !source) return std::unexpected(std::format("prefab has no node with local id {}", local_id));
  if (!component.empty() && (!target->find_component(component) || !source->find_component(component))) {
    return std::unexpected(std::format("prefab node {} has no component '{}'", local_id, component));
  }

  write_value(*target, component, property, value, registry);
  const auto base = read_value(*source, component, property, registry);
  if (!base) return std::unexpected(base.error());

  clear_prefab_override(instance_root, local_id, component, property);
  if (!same_value(*base, value)) {
    PrefabLink link = *instance_root.prefab_link();
    link.overrides.push_back(PrefabOverride{local_id, component, property, value});
    instance_root.set_prefab_link(std::move(link));
  }
  return {};
}

bool clear_prefab_override(Node& instance_root, NodeId local_id, const std::string& component,
                           const std::string& property) {
  if (!instance_root.prefab_link()) return false;
  PrefabLink link = *instance_root.prefab_link();
  const auto before = link.overrides.size();
  std::erase_if(link.overrides, [&](const PrefabOverride& o) { return same_key(o, local_id, component, property); });
  const bool removed = link.overrides.size() != before;
  if (removed) instance_root.set_prefab_link(std::move(link));
  return removed;
}

bool break_prefab_link(Node& node) {
  if (!node.prefab_link()) return false;
  const NodeId instance_id = node.prefab_link()->instance_id;
  Node* root = &node;
  while (root->id() != instance_id && root->parent()) root = root->parent();
  if (root->id() != instance_id) return false;
  unlink_subtree(*root, instance_id);
  return true;
}

}  // namespace arcane
