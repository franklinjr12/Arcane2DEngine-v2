#pragma once

#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/node.hpp"

namespace arcane {

class ComponentRegistry;

// Pooled prefab spawner (projectiles, enemies). Instances come from active_game_context()->spawn_prefab and are
// parked inactive (Node::set_active(false)) on release instead of being destroyed, then handed out again by spawn()
// with Component::on_pool_reuse called on their subtree. Instances are tracked by NodeId; ones destroyed by other
// code simply leave the pool. Destroying the spawner destroys every instance it owns. Runtime state is not saved.
class ARCANE_API Spawner2D : public Component {
 public:
  Spawner2D();
  ~Spawner2D() override;

  std::string prefab;          // project-relative *.prefab.json
  int prewarm = 0;             // inactive instances created in on_start
  int max_alive = 16;          // cap on handed-out instances; 0 = unlimited
  bool grow = true;            // instantiate when no free instance is left (below max_alive)
  float interval = 0.0f;       // seconds between automatic spawns; 0 = only manual spawn() calls
  bool auto_start = true;      // automatic spawning runs from the start (see start/stop)
  Vec2 offset{};               // added to the spawner's world position for automatic spawns
  bool spawn_as_root = true;   // instances are scene roots; otherwise children of the spawner node

  // Hands out an instance at scene-space `world_position` (RigidBody2D::teleport if it has a body), active.
  // Null at max_alive, when the pool is empty and grow is off, or if the prefab cannot be spawned (logged once).
  Node* spawn(Vec2 world_position);
  // Parks an instance of this spawner (inactive, back on the free list). False if `node` is not one of them.
  bool release(Node& node);
  int alive_count() const noexcept { return static_cast<int>(alive_.size()); }
  int free_count() const noexcept { return static_cast<int>(free_.size()); }

  // Starts / stops automatic interval spawning (auto_start decides the initial state).
  void start() noexcept { running_ = true; }
  void stop() noexcept { running_ = false; }
  bool running() const noexcept { return running_; }

  // Releases `node` to the spawner that owns it. False if it is not a pooled instance; callers then fall back to
  // scene()->destroy_node(node.id()).
  static bool release_to_pool(Node& node);

  void on_start() override;
  void on_update(float fixed_dt) override;
  void on_destroy() override;

 private:
  Node* create_instance();
  void prune();

  std::vector<NodeId> alive_;
  std::vector<NodeId> free_;
  float timer_ = 0.0f;
  bool running_ = true;
  bool error_logged_ = false;
};

ARCANE_API void register_spawner_components(ComponentRegistry& registry);

}  // namespace arcane
