#include <cmath>
#include <vector>

#include "arcane/component_registry.hpp"
#include "arcane/particles.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Fixture {
  arcane::ComponentRegistry registry;
  arcane::Scene scene;
  arcane::Node* node = nullptr;
  arcane::ParticleEmitter* emitter = nullptr;

  Fixture() {
    arcane::register_particle_components(registry);
    node = scene.create_node("fx");
    node->local_transform().position = {50.0f, 80.0f};
    emitter = static_cast<arcane::ParticleEmitter*>(scene.add_component(node, "ParticleEmitter", registry));
    emitter->lifetime_min = emitter->lifetime_max = 100.0f;  // nothing expires unless a test says so
    emitter->max_particles = 100000;
  }
  void step(int n) {
    for (int i = 0; i < n; ++i) emitter->on_update(kDt);
  }
};

int count_after(float rate, int steps) {
  Fixture f;
  f.emitter->rate = rate;
  f.step(steps);
  return static_cast<int>(f.emitter->particles().size());
}

void test_rate_spawn_counts() {
  ARCANE_ASSERT_TRUE(count_after(10.0f, 60) == 10);
  ARCANE_ASSERT_TRUE(count_after(60.0f, 60) == 60);
  ARCANE_ASSERT_TRUE(count_after(120.0f, 30) == 60);
  ARCANE_ASSERT_TRUE(count_after(0.0f, 60) == 0);
  ARCANE_ASSERT_TRUE(count_after(10.0f, 5) == 0);  // fractional accumulation: first spawn at step 6
  ARCANE_ASSERT_TRUE(count_after(10.0f, 6) == 1);
}

void test_burst_and_not_emitting() {
  Fixture f;
  f.emitter->rate = 0.0f;
  f.emitter->burst_count = 25;
  f.step(1);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 25);
  f.step(30);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 25);  // burst fires once

  f.emitter->restart();
  f.emitter->emitting = false;
  f.emitter->rate = 100.0f;
  f.step(1);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 25);  // restart re-arms the burst; emitting=false stops the rate
  f.step(60);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 25);
  ARCANE_ASSERT_TRUE(f.emitter->burst(5) == 5 && f.emitter->particles().size() == 30);
}

void test_lifetime_expiry() {
  Fixture f;
  f.emitter->rate = 0.0f;
  f.emitter->burst_count = 10;
  f.emitter->lifetime_min = f.emitter->lifetime_max = 0.5f;
  f.step(1);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 10);
  f.step(29);  // age 29/60 < 0.5
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 10);
  f.step(2);  // age 31/60 > 0.5
  ARCANE_ASSERT_TRUE(f.emitter->particles().empty());
}

void test_max_cap() {
  Fixture f;
  f.emitter->max_particles = 12;
  f.emitter->rate = 600.0f;
  f.emitter->burst_count = 50;
  f.step(1);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 12);
  f.step(60);
  ARCANE_ASSERT_TRUE(f.emitter->particles().size() == 12);
}

std::vector<arcane::Vec2> positions_for_seed(int seed) {
  Fixture f;
  f.emitter->seed = seed;
  f.emitter->rate = 30.0f;
  f.step(60);
  std::vector<arcane::Vec2> out;
  for (const auto& p : f.emitter->particles()) out.push_back(p.position);
  return out;
}

void test_deterministic_seed() {
  const auto a = positions_for_seed(5);
  ARCANE_ASSERT_TRUE(a.size() == 30);
  ARCANE_ASSERT_TRUE(a == positions_for_seed(5));
  ARCANE_ASSERT_TRUE(a != positions_for_seed(6));
}

void test_motion_gravity_and_space() {
  Fixture f;
  f.emitter->rate = 0.0f;
  f.emitter->burst_count = 1;
  f.emitter->speed_min = f.emitter->speed_max = 60.0f;
  f.emitter->angle_min = f.emitter->angle_max = 0.0f;  // +x
  f.emitter->gravity = {0.0f, 60.0f};
  f.step(1);
  const arcane::Particle first = f.emitter->particles()[0];
  ARCANE_ASSERT_TRUE(first.position == (arcane::Vec2{50.0f, 80.0f}));  // spawned at the node, world space
  f.step(60);
  const arcane::Particle& p = f.emitter->particles()[0];
  ARCANE_ASSERT_TRUE(std::fabs(p.velocity.x - 60.0f) < 0.01f);
  ARCANE_ASSERT_TRUE(std::fabs(p.velocity.y - 60.0f) < 0.1f);  // gravity pulls toward +y (down)
  ARCANE_ASSERT_TRUE(p.position.x > 100.0f && p.position.y > 80.0f);

  // Local space: spawned at the local origin.
  f.emitter->restart();
  f.emitter->world_space = false;
  f.step(1);
  ARCANE_ASSERT_TRUE(f.emitter->particles()[0].position == (arcane::Vec2{}));
}

void test_frame_uv() {
  Fixture f;
  f.emitter->atlas_columns = 4;
  f.emitter->atlas_rows = 2;
  f.emitter->frame = 5;  // column 1, row 1
  const arcane::UvRect uv = f.emitter->frame_uv();
  ARCANE_ASSERT_TRUE(uv.u0 == 0.25f && uv.u1 == 0.5f && uv.v0 == 0.5f && uv.v1 == 1.0f);
  f.emitter->frame = 99;  // clamped to the last cell
  ARCANE_ASSERT_TRUE(f.emitter->frame_uv().u0 == 0.75f);
}

void test_serialization_round_trip() {
  Fixture f;
  f.emitter->rate = 12.5f;
  f.emitter->burst_count = 7;
  f.emitter->gravity = {1.0f, 2.0f};
  f.emitter->start_color = {0.1f, 0.2f, 0.3f, 0.4f};
  f.emitter->end_size = 3.0f;
  f.emitter->texture = 42;
  f.emitter->atlas_columns = 3;
  f.emitter->world_space = false;
  f.emitter->seed = 99;
  f.emitter->max_particles = 77;
  auto loaded = arcane::Scene::load_from_json(f.scene.save_to_json(f.registry), f.registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  auto* r = static_cast<arcane::ParticleEmitter*>(loaded->roots()[0]->find_component("ParticleEmitter"));
  ARCANE_ASSERT_TRUE(r != nullptr);
  ARCANE_ASSERT_TRUE(r->rate == 12.5f && r->burst_count == 7 && r->gravity == (arcane::Vec2{1.0f, 2.0f}));
  ARCANE_ASSERT_TRUE(r->start_color.b == 0.3f && r->start_color.a == 0.4f && r->end_size == 3.0f);
  ARCANE_ASSERT_TRUE(r->texture == 42 && r->atlas_columns == 3 && !r->world_space);
  ARCANE_ASSERT_TRUE(r->seed == 99 && r->max_particles == 77 && r->particles().empty());
}

}  // namespace

int main() {
  test_rate_spawn_counts();
  test_burst_and_not_emitting();
  test_lifetime_expiry();
  test_max_cap();
  test_deterministic_seed();
  test_motion_gravity_and_space();
  test_frame_uv();
  test_serialization_round_trip();
  return 0;
}
