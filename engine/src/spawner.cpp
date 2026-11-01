#include "arcane/spawner.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "arcane/component_registry.hpp"
#include "arcane/game_context.hpp"
#include "arcane/log.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"

namespace arcane {

namespace {

// Runtime-only pool membership: instance node id -> owning spawner. Node ids are per scene, so lookups also
// compare the scenes. Entries are removed when an instance leaves the pool or its spawner is destroyed.
std::unordered_multimap<NodeId, Spawner2D*>& pool_members() {
  static std::unordered_multimap<NodeId, Spawner2D*> members;
  return members;
}

void erase_member(NodeId id, const Spawner2D* spawner) {
  auto [first, last] = pool_members().equal_range(id);
  for (auto it = first; it != last; ++it) {
    if (it->second == spawner) {
      pool_members().erase(it);
      return;
    }
  }
}

bool erase_id(std::vector<NodeId>& ids, NodeId id) {
  auto it = std::find(ids.begin(), ids.end(), id);
  if (it == ids.end()) return false;
  ids.erase(it);
  return true;
}

void call_pool_reuse(Node& node) {
  for (const auto& component : node.components()) component->on_pool_reuse();
  for (const auto& child : node.children()) call_pool_reuse(*child);
}

// `world` expressed in `parent`'s local space (inverse of Node::world_transform's composition).
Vec2 to_local(const Node* parent, Vec2 world) {
  if (!parent) return world;
  const WorldTransform2D p = parent->world_transform();
  const float dx = world.x - p.position.x, dy = world.y - p.position.y;
  const float c = std::cos(p.rotation_radians), s = std::sin(p.rotation_radians);
  const float sx = p.scale.x != 0.0f ? p.scale.x : 1.0f, sy = p.scale.y != 0.0f ? p.scale.y : 1.0f;
  return Vec2{(dx * c + dy * s) / sx, (-dx * s + dy * c) / sy};
}

}  // namespace

Spawner2D::Spawner2D() = default;

Spawner2D::~Spawner2D() {
  for (NodeId id : alive_) erase_member(id, this);
  for (NodeId id : free_) erase_member(id, this);
}

void Spawner2D::prune() {
  Scene* owner = scene();
  auto gone = [&](NodeId id) {
    Node* node = owner ? owner->find_node(id) : nullptr;
    if (node && !node->pending_destroy()) return false;
    erase_member(id, this);
    return true;
  };
  std::erase_if(alive_, gone);
  std::erase_if(free_, gone);
}

Node* Spawner2D::create_instance() {
  GameContext* context = active_game_context();
  if (!context || !scene()) {
    if (!error_logged_) ARCANE_LOG_ERROR("spawner", "Spawner2D '{}': no active game context", prefab);
    error_logged_ = true;
    return nullptr;
  }
  auto made = context->spawn_prefab(*scene(), prefab, spawn_as_root ? nullptr : node());
  if (!made) {
    if (!error_logged_) ARCANE_LOG_ERROR("spawner", "Spawner2D '{}': {}", prefab, made.error());
    error_logged_ = true;
    return nullptr;
  }
  pool_members().emplace((*made)->id(), this);
  return *made;
}

Node* Spawner2D::spawn(Vec2 world_position) {
  prune();
  if (max_alive > 0 && alive_count() >= max_alive) return nullptr;
  Node* instance = nullptr;
  bool reused = false;
  if (!free_.empty()) {
    instance = scene()->find_node(free_.back());
    free_.pop_back();
    reused = true;
  } else if (grow) {
    instance = create_instance();
  }
  if (!instance) return nullptr;

  const Vec2 local = to_local(instance->parent(), world_position);
  if (auto* body = dynamic_cast<RigidBody2D*>(instance->find_component("RigidBody2D"))) body->teleport(world_position);
  instance->local_transform().position = local;
  instance->set_active(true);
  alive_.push_back(instance->id());
  if (reused) call_pool_reuse(*instance);
  return instance;
}

bool Spawner2D::release(Node& node) {
  if (!erase_id(alive_, node.id())) {
    return std::find(free_.begin(), free_.end(), node.id()) != free_.end();  // already parked
  }
  node.set_active(false);
  free_.push_back(node.id());
  return true;
}

bool Spawner2D::release_to_pool(Node& node) {
  auto [first, last] = pool_members().equal_range(node.id());
  for (auto it = first; it != last; ++it) {
    Spawner2D* spawner = it->second;
    if (spawner->scene() == node.scene()) return spawner->release(node);
  }
  return false;
}

void Spawner2D::on_start() {
  running_ = auto_start;
  for (int i = 0; i < prewarm; ++i) {
    Node* instance = create_instance();
    if (!instance) break;
    instance->set_active(false);
    free_.push_back(instance->id());
  }
}

void Spawner2D::on_update(float fixed_dt) {
  if (interval <= 0.0f || !running_) return;
  timer_ += fixed_dt;
  while (timer_ >= interval) {
    timer_ -= interval;
    const Vec2 origin = node()->world_transform().position;
    spawn(Vec2{origin.x + offset.x, origin.y + offset.y});
  }
}

void Spawner2D::on_destroy() {
  Scene* owner = scene();
  for (const std::vector<NodeId>* ids : {&alive_, &free_}) {
    for (NodeId id : *ids) {
      erase_member(id, this);
      if (owner) owner->destroy_node(id);
    }
  }
  alive_.clear();
  free_.clear();
}

void register_spawner_components(ComponentRegistry& registry) {
  registry.register_component(
      "Spawner2D", [] { return std::make_unique<Spawner2D>(); },
      {
          make_property<Spawner2D>("prefab", PropertyType::String, &Spawner2D::prefab),
          make_property<Spawner2D>("prewarm", PropertyType::Int, &Spawner2D::prewarm),
          make_property<Spawner2D>("max_alive", PropertyType::Int, &Spawner2D::max_alive),
          make_property<Spawner2D>("grow", PropertyType::Bool, &Spawner2D::grow),
          make_property<Spawner2D>("interval", PropertyType::Float, &Spawner2D::interval),
          make_property<Spawner2D>("auto_start", PropertyType::Bool, &Spawner2D::auto_start),
          make_property<Spawner2D>("offset", PropertyType::Vec2, &Spawner2D::offset),
          make_property<Spawner2D>("spawn_as_root", PropertyType::Bool, &Spawner2D::spawn_as_root),
      });
}

}  // namespace arcane
