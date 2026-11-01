#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/node.hpp"

namespace arcane {

class ComponentRegistry;

struct HealthEvent {
  enum class Kind { Damaged, Healed, Died };
  Kind kind = Kind::Damaged;
  float amount = 0.0f;   // damage actually applied / health actually restored
  float current = 0.0f;  // health after the change
  NodeId source = 0;     // damaging node (0 = none/unknown); always 0 for Healed
};

// Hit points with a post-hit invulnerability window. Runtime state (current health, timer, dead flag) is never saved
// and is reset in on_start and on_pool_reuse. Death handling: release to the owning Spawner2D pool when
// `release_on_death` and the node is pooled; else Scene::destroy_node (deferred to the end of the tick) when
// `destroy_on_death`; else the node simply stays dead.
class ARCANE_API Health : public Component {
 public:
  float max_health = 5.0f;
  float invulnerability_time = 0.5f;  // seconds of immunity after each damaging hit
  bool destroy_on_death = false;
  bool release_on_death = true;

  // Returns the damage applied: 0 while invulnerable or dead or for amount <= 0; otherwise min(amount, current()).
  float apply_damage(float amount, Node* source);
  float heal(float amount);  // clamped to max_health; returns the amount restored (0 when dead)
  void reset();              // full health, alive, no invulnerability (listeners are kept)
  void kill();               // dies at once (ignores invulnerability); no-op when already dead

  float current() const noexcept;
  bool dead() const noexcept { return dead_; }
  bool invulnerable() const noexcept { return invulnerable_timer_ > 0.0f; }

  // Listeners run synchronously from apply_damage/heal/kill. Ids are positive and never reused.
  int add_listener(std::function<void(const HealthEvent&)> listener);
  void remove_listener(int id);
  const std::optional<HealthEvent>& last_event() const noexcept { return last_event_; }

  void on_start() override;
  void on_update(float fixed_dt) override;
  void on_pool_reuse() override;

 private:
  void emit(const HealthEvent& event);
  void die(NodeId source, float amount);

  mutable float current_ = -1.0f;  // < 0: not initialised yet (starts at max_health)
  float invulnerable_timer_ = 0.0f;
  bool dead_ = false;
  std::optional<HealthEvent> last_event_;
  std::vector<std::pair<int, std::function<void(const HealthEvent&)>>> listeners_;
  int next_listener_ = 1;
};

// Marks a node (normally with a solid or sensor Collider2D) as damageable by Hitbox.
class ARCANE_API Hurtbox : public Component {
 public:
  float damage_multiplier = 1.0f;
  std::string health_node;  // name of the node holding the Health; empty = nearest Health on this node or ancestors

  // The Health this hurtbox feeds, or null. Do not keep it past the current tick.
  Health* health() const;
  // Applies `damage * damage_multiplier` to health(); returns the damage applied.
  float hurt(float damage, Node* source) const;
};

// Deals damage to every Hurtbox that overlaps the sibling Collider2D (normally a sensor). Teams and friendly fire
// are not modelled here: use Collider2D layer/mask so a hitbox only ever overlaps the hurtboxes it may damage.
class ARCANE_API Hitbox : public Component {
 public:
  float damage = 1.0f;
  float rehit_interval = 0.5f;  // seconds before the same target is hit again while still overlapping; 0 = once per overlap
  bool active = true;
  bool ignore_own_hierarchy = true;  // skip hurtboxes on this node, its ancestors and its descendants

  void on_update(float fixed_dt) override;
  void on_trigger_enter(Node& other) override { begin_overlap(other); }
  void on_trigger_exit(Node& other) override;
  void on_collision_enter(Node& other) override { begin_overlap(other); }
  void on_collision_exit(Node& other) override;
  void on_pool_reuse() override { targets_.clear(); }

  // Number of hurtbox hits dealt since the last reset (runtime-only).
  int hits_dealt() const noexcept { return hits_dealt_; }

 private:
  struct Target {
    NodeId id = 0;
    float wait = 0.0f;  // seconds until the next hit is allowed
    bool hit_once = false;
  };
  void begin_overlap(Node& other);
  void end_overlap(Node& other);
  bool try_hit(Target& target);

  std::vector<Target> targets_;
  int hits_dealt_ = 0;
};

// Moves the node along `direction` at `speed` by driving the sibling RigidBody2D velocity each update. Use a Dynamic
// body with gravity_scale 0 (Kinematic also works) and a *sensor* Collider2D: Box2D reports sensor overlaps only
// between a sensor and a non-sensor shape, so hurtboxes and walls must be solid colliders. A sibling Hitbox does the
// damage. Despawns (Spawner2D::release_to_pool, else Scene::destroy_node) when `lifetime` runs out, when it touches
// a solid non-hurtbox collider (if `stop_on_solid`), or after hitting more than `pierce` hurtboxes.
class ARCANE_API Projectile : public Component {
 public:
  float speed = 300.0f;
  Vec2 direction{1.0f, 0.0f};
  float lifetime = 2.0f;
  int pierce = 0;  // extra hurtboxes it may pass through
  bool stop_on_solid = true;

  void launch(Vec2 dir, float new_speed);

  void on_update(float fixed_dt) override;
  void on_trigger_enter(Node& other) override { touched(other); }
  void on_collision_enter(Node& other) override { touched(other); }
  void on_pool_reuse() override;

  int hurtbox_hits() const noexcept { return hits_; }

 private:
  void touched(Node& other);
  void despawn();
  void drive();

  float age_ = 0.0f;
  int hits_ = 0;
  bool done_ = false;
};

ARCANE_API void register_combat_components(ComponentRegistry& registry);

}  // namespace arcane
