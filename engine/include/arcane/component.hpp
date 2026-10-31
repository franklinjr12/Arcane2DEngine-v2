#pragma once

#include <cstdint>
#include <string>

#include "arcane/export.hpp"

namespace arcane {

using ComponentId = std::uint64_t;
inline constexpr ComponentId kInvalidComponentId = 0;

class Node;
class Scene;

// Base class for data/behaviour attached to a Node. Owned by the Node it is
// attached to (destroyed when the node is destroyed). Lifecycle hooks are
// called by Scene::update(): on_create() runs once, synchronously, when the
// component is attached; on_start() runs once, the first time the owning
// scene is updated after attachment; on_update() runs on every subsequent
// Scene::update() call (including the one where on_start ran, immediately
// after it); on_destroy() runs once, when the owning node's subtree is
// actually removed (see Scene::destroy_node).
class ARCANE_API Component {
 public:
  virtual ~Component() = default;

  virtual void on_create() {}
  virtual void on_start() {}
  virtual void on_update(float fixed_dt) { (void)fixed_dt; }
  virtual void on_destroy() {}

  // Physics notifications (see arcane/physics.hpp), during Play only, called on every component of both nodes
  // after the step that caused them, ordered by node ids. Collision events are for solid shapes that start or
  // stop touching; trigger events are for a sensor Collider2D and the shape that starts or stops overlapping it
  // (the sensor's node gets the visitor as `other` and vice versa). Default no-ops.
  virtual void on_collision_enter(Node& other) { (void)other; }
  virtual void on_collision_exit(Node& other) { (void)other; }
  virtual void on_trigger_enter(Node& other) { (void)other; }
  virtual void on_trigger_exit(Node& other) { (void)other; }

  ComponentId id() const noexcept { return id_; }
  Node* node() const noexcept { return node_; }
  // The scene owning this component's node (null if detached); see Node::scene().
  Scene* scene() const noexcept;
  const std::string& type_name() const noexcept { return type_name_; }

 private:
  friend class Node;
  friend class Scene;

  ComponentId id_ = kInvalidComponentId;
  Node* node_ = nullptr;
  std::string type_name_;
  bool started_ = false;
};

}  // namespace arcane
