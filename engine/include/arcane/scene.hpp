#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/export.hpp"
#include "arcane/node.hpp"

namespace arcane {

// Owns a tree of Nodes (and, through them, Components). Node/Component IDs
// are assigned from a single monotonically increasing per-scene counter
// (see docs/decisions.md): simpler than random 64-bit IDs or per-kind
// counters, no collision handling needed, and the counter itself is
// persisted (next_id in the saved JSON) so reloading a scene never reuses
// an ID that existed before, even if the node that held it was deleted.
class ARCANE_API Scene {
 public:
  Scene() = default;
  // Moves re-point every node's scene() at the new owner.
  Scene(Scene&& other) noexcept;
  Scene& operator=(Scene&& other) noexcept;
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  // Creates an owned node; parent == nullptr makes it a root. Must belong
  // to this scene if non-null.
  Node* create_node(std::string name, Node* parent = nullptr);

  Node* find_node(NodeId id) const noexcept;
  Component* find_component(ComponentId id) const noexcept;

  // Looks up `type_name` in `registry` and attaches a new instance to
  // `node`. Returns nullptr if the type is not registered.
  Component* add_component(Node* node, const std::string& type_name, const ComponentRegistry& registry);

  // Detaches and destroys `component` (calling its on_destroy()), keeping the
  // IDs of everything else. Returns false and does nothing for null or a
  // component that is not attached to a node of this scene.
  bool remove_component(Component* component);

  // Moves `node` (and its subtree) under `new_parent` (nullptr = becomes a
  // root). Returns false and does nothing if new_parent is node itself or a
  // descendant of node (which would create a cycle).
  bool reparent(Node* node, Node* new_parent);

  // Moves the subtree of root node `root` out of `from` and into this scene
  // as a root, keeping the live Node/Component objects (and their runtime
  // state). Every node and component gets a fresh ID from this scene's
  // counter; EntityId properties that pointed inside the subtree follow, any
  // other node reference is cleared. Returns the node, or nullptr (nothing
  // moved) if `root` is not a root of `from`, is pending destroy, or `from`
  // is this scene. Used by SceneManager to keep persistent nodes.
  Node* adopt_root(Scene& from, Node* root, const ComponentRegistry& registry);

  // Marks `id`'s node and its whole subtree for destruction. Safe to call
  // while iterating the scene (e.g. from a component's on_update): nodes
  // are only unlinked and their components' on_destroy() called during the
  // next flush_destroyed(), which Scene::update() runs automatically after
  // each tick.
  void destroy_node(NodeId id);
  void flush_destroyed();

  // Runs one fixed-timestep tick over every live component (on_start on
  // first tick, on_update on every tick including that one), then flushes
  // any destructions queued during the tick.
  void update(float fixed_dt);

  // First live (not pending-destroy) node named `name`, depth-first, or nullptr.
  Node* find_node_by_name(const std::string& name) const noexcept;

  const std::vector<std::unique_ptr<Node>>& roots() const noexcept { return roots_; }

  NodeId next_id() const noexcept { return next_id_; }
  // Only for restoring a scene from disk: see Scene::load_from_json.
  void set_next_id(NodeId value) noexcept { next_id_ = value; }

  // Serializes this scene as canonical scene v2 (docs/formats/scene.md),
  // using `registry` to know which properties each attached component
  // declares. Fails on non-finite floats or invalid UTF-8 names.
  std::expected<std::string, std::string> save_canonical_json(const ComponentRegistry& registry) const;

  // Convenience wrapper over save_canonical_json; returns an empty string
  // on failure (use save_canonical_json to learn why).
  std::string save_to_json(const ComponentRegistry& registry) const;

  // Parses a scene file: canonical v2 (checked strictly: unknown fields,
  // properties and wrong value types are errors) or legacy format_version 1
  // (unknown properties on known types are ignored, as before). Fails on
  // malformed JSON, a missing/unsupported/newer version, or a component
  // whose registered type is not found in `registry`.
  static std::expected<Scene, std::string> load_from_json(const std::string& text, const ComponentRegistry& registry);

  // Like load_from_json, but legacy files are checked as strictly as v2
  // ones, so nothing in the file can be silently dropped. Used by migration.
  static std::expected<Scene, std::string> load_from_json_strict(const std::string& text,
                                                                 const ComponentRegistry& registry);

 private:
  std::uint64_t allocate_id() noexcept { return next_id_++; }

  static std::expected<Scene, std::string> load_scene(const std::string& text, const ComponentRegistry& registry,
                                                      bool strict_unknown);

  Node* create_node_with_id(NodeId id, std::string name, Node* parent);
  Component* add_component_with_id(Node* node, const std::string& type_name, ComponentId id,
                                    const ComponentRegistry& registry);

  bool is_descendant_of(const Node* candidate, const Node* ancestor) const noexcept;
  void mark_pending_destroy(Node* node);
  void adopt_nodes() noexcept;  // points every node's scene_ at this scene
  void collect_preorder(std::vector<Component*>& out) const;
  void collect_preorder(const std::unique_ptr<Node>& node, std::vector<Component*>& out) const;
  void unlink_and_destroy(std::unique_ptr<Node> node);

  std::vector<std::unique_ptr<Node>> roots_;
  std::unordered_map<NodeId, Node*> nodes_by_id_;
  std::unordered_map<ComponentId, Component*> components_by_id_;
  std::uint64_t next_id_ = 1;
};

}  // namespace arcane
