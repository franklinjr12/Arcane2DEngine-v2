#include "arcane/combat.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/spawner.hpp"

namespace arcane {

namespace {

bool is_ancestor_or_self(const Node* maybe_ancestor, const Node* node) {
  for (; node; node = node->parent()) {
    if (node == maybe_ancestor) return true;
  }
  return false;
}

bool in_own_hierarchy(const Node& a, const Node& b) { return is_ancestor_or_self(&a, &b) || is_ancestor_or_self(&b, &a); }

void remove_node(Node& node) {
  if (Spawner2D::release_to_pool(node)) return;
  if (Scene* scene = node.scene()) scene->destroy_node(node.id());
}

}  // namespace

float Health::current() const noexcept {
  if (current_ < 0.0f) current_ = std::max(max_health, 0.0f);
  return current_;
}

void Health::emit(const HealthEvent& event) {
  last_event_ = event;
  const auto snapshot = listeners_;  // listeners may add or remove listeners
  for (const auto& [id, listener] : snapshot) {
    if (listener) listener(event);
  }
}

float Health::apply_damage(float amount, Node* source) {
  const float before = current();
  if (dead_ || invulnerable_timer_ > 0.0f || !(amount > 0.0f)) return 0.0f;
  const float applied = std::min(amount, before);
  current_ = before - applied;
  invulnerable_timer_ = std::max(invulnerability_time, 0.0f);
  const NodeId source_id = source ? source->id() : 0;
  if (current_ <= 0.0f) {
    die(source_id, applied);
  } else {
    emit({HealthEvent::Kind::Damaged, applied, current_, source_id});
  }
  return applied;
}

float Health::heal(float amount) {
  const float before = current();
  if (dead_ || !(amount > 0.0f)) return 0.0f;
  current_ = std::min(before + amount, std::max(max_health, 0.0f));
  const float restored = current_ - before;
  if (restored > 0.0f) emit({HealthEvent::Kind::Healed, restored, current_, 0});
  return restored;
}

void Health::reset() {
  current_ = std::max(max_health, 0.0f);
  invulnerable_timer_ = 0.0f;
  dead_ = false;
  last_event_.reset();
}

void Health::kill() {
  if (dead_) return;
  const float before = current();
  current_ = 0.0f;
  die(0, before);
}

void Health::die(NodeId source, float amount) {
  dead_ = true;
  current_ = 0.0f;
  emit({HealthEvent::Kind::Died, amount, 0.0f, source});
  Node* owner = node();
  if (!owner) return;
  if (release_on_death && Spawner2D::release_to_pool(*owner)) return;
  if (destroy_on_death && owner->scene()) owner->scene()->destroy_node(owner->id());
}

int Health::add_listener(std::function<void(const HealthEvent&)> listener) {
  const int id = next_listener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void Health::remove_listener(int id) {
  std::erase_if(listeners_, [id](const auto& entry) { return entry.first == id; });
}

void Health::on_start() { reset(); }

void Health::on_update(float fixed_dt) {
  if (invulnerable_timer_ > 0.0f) invulnerable_timer_ = std::max(invulnerable_timer_ - fixed_dt, 0.0f);
}

void Health::on_pool_reuse() { reset(); }

Health* Hurtbox::health() const {
  const Node* owner = node();
  if (!owner) return nullptr;
  if (!health_node.empty()) {
    Scene* owner_scene = owner->scene();
    Node* target = owner_scene ? owner_scene->find_node_by_name(health_node) : nullptr;
    return target ? static_cast<Health*>(target->find_component("Health")) : nullptr;
  }
  for (const Node* n = owner; n; n = n->parent()) {
    if (auto* h = static_cast<Health*>(n->find_component("Health"))) return h;
  }
  return nullptr;
}

float Hurtbox::hurt(float damage, Node* source) const {
  Health* h = health();
  return h ? h->apply_damage(damage * damage_multiplier, source) : 0.0f;
}

void Hitbox::begin_overlap(Node& other) {
  Node* owner = node();
  if (!owner) return;
  const NodeId id = other.id();
  auto it = std::find_if(targets_.begin(), targets_.end(), [id](const Target& t) { return t.id == id; });
  if (it == targets_.end()) {
    targets_.push_back({id, 0.0f, false});
    it = targets_.end() - 1;
  }
  if (active) try_hit(*it);
}

void Hitbox::end_overlap(Node& other) {
  const NodeId id = other.id();
  std::erase_if(targets_, [id](const Target& t) { return t.id == id; });
}

void Hitbox::on_trigger_exit(Node& other) { end_overlap(other); }
void Hitbox::on_collision_exit(Node& other) { end_overlap(other); }

bool Hitbox::try_hit(Target& target) {
  Node* owner = node();
  Scene* owner_scene = owner ? owner->scene() : nullptr;
  Node* other = owner_scene ? owner_scene->find_node(target.id) : nullptr;
  if (!other || other->pending_destroy()) return false;
  if (ignore_own_hierarchy && in_own_hierarchy(*owner, *other)) return false;
  auto* hurtbox = static_cast<Hurtbox*>(other->find_component("Hurtbox"));
  if (!hurtbox) return false;
  target.hit_once = true;
  target.wait = std::max(rehit_interval, 0.0f);
  ++hits_dealt_;
  hurtbox->hurt(damage, owner);
  return true;
}

void Hitbox::on_update(float fixed_dt) {
  if (targets_.empty()) return;
  Scene* owner_scene = scene();
  // try_hit never edits targets_; destroying nodes from listeners is deferred, so the loop stays valid.
  for (auto& target : targets_) {
    if (target.wait > 0.0f) target.wait -= fixed_dt;
    if (!active || target.wait > 0.0f) continue;
    if (target.hit_once && rehit_interval <= 0.0f) continue;
    if (!owner_scene || !owner_scene->find_node(target.id)) continue;
    try_hit(target);
  }
  std::erase_if(targets_, [owner_scene](const Target& t) {
    Node* n = owner_scene ? owner_scene->find_node(t.id) : nullptr;
    return !n || n->pending_destroy();
  });
}

void Projectile::launch(Vec2 dir, float new_speed) {
  const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (length > 0.0f) direction = {dir.x / length, dir.y / length};
  speed = new_speed;
  drive();
}

void Projectile::drive() {
  Node* owner = node();
  if (!owner) return;
  auto* body = static_cast<RigidBody2D*>(owner->find_component("RigidBody2D"));
  if (!body) return;
  const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
  if (length <= 0.0f) {
    body->set_velocity({});
    return;
  }
  body->set_velocity({direction.x / length * speed, direction.y / length * speed});
}

void Projectile::despawn() {
  if (done_) return;
  done_ = true;
  if (Node* owner = node()) remove_node(*owner);
}

void Projectile::on_update(float fixed_dt) {
  if (done_) return;
  age_ += fixed_dt;
  if (age_ >= lifetime) {
    despawn();
    return;
  }
  drive();
}

void Projectile::touched(Node& other) {
  if (done_ || !node()) return;
  if (in_own_hierarchy(*node(), other)) return;
  if (other.find_component("Hurtbox")) {
    if (++hits_ > pierce) despawn();
    return;
  }
  auto* collider = static_cast<Collider2D*>(other.find_component("Collider2D"));
  if (stop_on_solid && collider && !collider->is_sensor) despawn();
}

void Projectile::on_pool_reuse() {
  age_ = 0.0f;
  hits_ = 0;
  done_ = false;
}

void register_combat_components(ComponentRegistry& registry) {
  {
    using C = Health;
    registry.register_component(
        "Health", [] { return std::make_unique<C>(); },
        {
            make_property<C>("max_health", PropertyType::Float, &C::max_health),
            make_property<C>("invulnerability_time", PropertyType::Float, &C::invulnerability_time),
            make_property<C>("destroy_on_death", PropertyType::Bool, &C::destroy_on_death),
            make_property<C>("release_on_death", PropertyType::Bool, &C::release_on_death),
        });
  }
  {
    using C = Hurtbox;
    registry.register_component(
        "Hurtbox", [] { return std::make_unique<C>(); },
        {
            make_property<C>("damage_multiplier", PropertyType::Float, &C::damage_multiplier),
            make_property<C>("health_node", PropertyType::String, &C::health_node),
        });
  }
  {
    using C = Hitbox;
    registry.register_component(
        "Hitbox", [] { return std::make_unique<C>(); },
        {
            make_property<C>("damage", PropertyType::Float, &C::damage),
            make_property<C>("rehit_interval", PropertyType::Float, &C::rehit_interval),
            make_property<C>("active", PropertyType::Bool, &C::active),
            make_property<C>("ignore_own_hierarchy", PropertyType::Bool, &C::ignore_own_hierarchy),
        });
  }
  {
    using C = Projectile;
    registry.register_component(
        "Projectile", [] { return std::make_unique<C>(); },
        {
            make_property<C>("speed", PropertyType::Float, &C::speed),
            make_property<C>("direction", PropertyType::Vec2, &C::direction),
            make_property<C>("lifetime", PropertyType::Float, &C::lifetime),
            make_property<C>("pierce", PropertyType::Int, &C::pierce),
            make_property<C>("stop_on_solid", PropertyType::Bool, &C::stop_on_solid),
        });
  }
}

}  // namespace arcane
