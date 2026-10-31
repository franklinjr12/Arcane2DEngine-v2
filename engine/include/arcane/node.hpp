#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/property.hpp"

namespace arcane {

using NodeId = std::uint64_t;
inline constexpr NodeId kInvalidNodeId = 0;

// Local 2D transform relative to the parent node (or to the scene origin,
// for a root node).
struct Transform2D {
  Vec2 position{};
  float rotation_radians = 0.0f;
  Vec2 scale{1.0f, 1.0f};
};

// Resolved transform in scene space, after composing the full parent chain.
using WorldTransform2D = Transform2D;

// One recorded difference between a prefab instance and its prefab file.
// `local_id` is the prefab-local node ID. With an empty `component` the
// property is a node field ("name", "position", "rotation_radians", "scale");
// otherwise it is a property of the node's component of that type.
struct PrefabOverride {
  NodeId local_id = kInvalidNodeId;
  std::string component;
  std::string property;
  PropertyValue value;
};

// Marks a node as part of a prefab instance (docs/formats/prefab.md).
// `instance_id` is the scene ID of the instance's root node; `overrides` is
// only filled on that root node.
struct PrefabLink {
  std::string asset;
  NodeId instance_id = kInvalidNodeId;
  NodeId local_id = kInvalidNodeId;
  std::vector<PrefabOverride> overrides;
};

class Scene;

// A node in the scene hierarchy. Owned by its parent (or by the Scene, for a
// root node) via std::unique_ptr; children/components share that lifetime.
// Its id is assigned once by Scene::create_node and never changes or is
// reused within that scene.
class ARCANE_API Node {
 public:
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  NodeId id() const noexcept { return id_; }
  const std::string& name() const noexcept { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }

  Transform2D& local_transform() noexcept { return local_; }
  const Transform2D& local_transform() const noexcept { return local_; }

  // Composes this node's local transform with every ancestor's, root to
  // leaf. Recomputed on every call (no caching) since Foundation B has no
  // per-frame transform propagation yet.
  WorldTransform2D world_transform() const noexcept;

  // The scene that owns this node (kept up to date when the Scene is moved). Lets components spawn and destroy
  // nodes from their hooks: scene()->destroy_node(id) is deferred to the end of the tick.
  Scene* scene() const noexcept { return scene_; }

  Node* parent() const noexcept { return parent_; }
  const std::vector<std::unique_ptr<Node>>& children() const noexcept { return children_; }

  const std::vector<std::unique_ptr<Component>>& components() const noexcept { return components_; }
  Component* find_component(std::string_view type_name) const noexcept;

  const std::optional<PrefabLink>& prefab_link() const noexcept { return prefab_link_; }
  void set_prefab_link(std::optional<PrefabLink> link) { prefab_link_ = std::move(link); }

  // True once Scene::destroy_node has marked this node (or an ancestor) for
  // destruction; the node still physically exists until the next flush.
  bool pending_destroy() const noexcept { return pending_destroy_; }

 private:
  friend class Scene;

  Node(NodeId id, std::string name) : id_(id), name_(std::move(name)) {}

  Component* attach_component(std::unique_ptr<Component> component, std::string type_name,
                               ComponentId component_id);

  NodeId id_;
  std::string name_;
  Node* parent_ = nullptr;
  Scene* scene_ = nullptr;
  std::vector<std::unique_ptr<Node>> children_;
  Transform2D local_{};
  std::vector<std::unique_ptr<Component>> components_;
  std::optional<PrefabLink> prefab_link_;
  bool pending_destroy_ = false;
};

}  // namespace arcane
