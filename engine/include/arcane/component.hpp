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

  // Physics contact notifications (see arcane/physics.hpp): called on every
  // component attached to either node when their Collider2D shapes start or
  // stop touching, during Play only. Default no-op so most components never
  // need to know about these.
  virtual void on_contact_begin(Node& other) { (void)other; }
  virtual void on_contact_end(Node& other) { (void)other; }

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
