#pragma once

#include <expected>
#include <optional>
#include <string>

#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"

namespace arcane {

// A reusable node subtree (docs/formats/prefab.md). Holds a private Scene
// whose single root is the prefab root; its node and component IDs are the
// prefab-local IDs (1..N in depth-first order) that scene instances refer
// to in their overrides. Prefabs do not nest: capturing an instance flattens
// it.
class ARCANE_API Prefab {
 public:
  // Copies `root` and its subtree. EntityId properties that point inside the
  // subtree are remapped to local IDs; ones pointing outside are cleared to 0.
  static std::expected<Prefab, std::string> from_node(const Node& root, const ComponentRegistry& registry);

  // Parses `*.prefab.json` text (strict).
  static std::expected<Prefab, std::string> from_json(const std::string& text, const ComponentRegistry& registry);

  std::expected<std::string, std::string> to_json(const ComponentRegistry& registry) const;

  const Scene& scene() const noexcept { return scene_; }
  const Node& root() const noexcept { return *scene_.roots().front(); }

 private:
  explicit Prefab(Scene scene) : scene_(std::move(scene)) {}
  Scene scene_;
};

// Creates a fresh copy of `prefab` in `scene` under `parent` (nullptr = a
// root) with new scene IDs, links every new node to `asset` (the
// project-relative prefab path) and remaps EntityId properties between
// copied nodes. Returns the instance root, whose ID is the instance ID.
ARCANE_API std::expected<Node*, std::string> instantiate_prefab(Scene& scene, const Prefab& prefab,
                                                                const std::string& asset, Node* parent,
                                                                const ComponentRegistry& registry);

// Sets one value on a prefab instance (`instance_root` is its root node) and
// records it as an override unless it equals the prefab's value, in which
// case an existing override is dropped. `component` empty = a node field.
// Fails for an unknown node, component or property, a value of the wrong
// type, or the Tilemap `layers` property.
ARCANE_API std::expected<void, std::string> set_prefab_override(Node& instance_root, const Prefab& prefab,
                                                                NodeId local_id, const std::string& component,
                                                                const std::string& property,
                                                                const PropertyValue& value,
                                                                const ComponentRegistry& registry);

// Removes the override record (the instance keeps its current value).
ARCANE_API bool clear_prefab_override(Node& instance_root, NodeId local_id, const std::string& component,
                                      const std::string& property);

// Turns the instance into plain nodes: all links and overrides are removed,
// node IDs and values stay. False if `node` is not part of an instance.
ARCANE_API bool break_prefab_link(Node& node);

// The type of an overridable property: node fields have fixed types,
// components use their registered descriptors.
ARCANE_API std::optional<PropertyType> prefab_override_type(const ComponentRegistry& registry,
                                                            const std::string& component,
                                                            const std::string& property);

}  // namespace arcane
