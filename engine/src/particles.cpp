#include "arcane/particles.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"

namespace arcane {

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

ParticleEmitter::ParticleEmitter() = default;
ParticleEmitter::~ParticleEmitter() = default;

float ParticleEmitter::random01() {
  if (!rng_seeded_) {
    rng_state_ = static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed));
    rng_seeded_ = true;
  }
  // splitmix64: tiny, and identical on every compiler/platform.
  rng_state_ += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = rng_state_;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return static_cast<float>(z >> 40) / static_cast<float>(1u << 24);
}

void ParticleEmitter::spawn() {
  const WorldTransform2D world = node()->world_transform();
  const float angle = lerp(angle_min, angle_max, random01()) * kDegToRad + world.rotation_radians;
  const float speed = lerp(speed_min, speed_max, random01());
  Particle p;
  p.lifetime = std::max(0.001f, lerp(lifetime_min, lifetime_max, random01()));
  p.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
  p.position = world_space ? world.position : Vec2{};
  particles_.push_back(p);
}

int ParticleEmitter::burst(int count) {
  const int room = std::max(0, max_particles - static_cast<int>(particles_.size()));
  const int n = std::clamp(count, 0, room);
  for (int i = 0; i < n; ++i) spawn();
  return n;
}

void ParticleEmitter::restart() {
  particles_.clear();
  rate_accumulator_ = 0.0;
  rng_seeded_ = false;
  burst_done_ = false;
}

void ParticleEmitter::on_update(float dt) {
  if (!node() || dt <= 0.0f) return;

  // Advance and expire existing particles (swap-free erase keeps spawn order stable for drawing).
  for (Particle& p : particles_) {
    p.age += dt;
    p.velocity.x += gravity.x * dt;
    p.velocity.y += gravity.y * dt;
    p.position.x += p.velocity.x * dt;
    p.position.y += p.velocity.y * dt;
  }
  std::erase_if(particles_, [](const Particle& p) { return p.age >= p.lifetime; });

  if (!burst_done_) {
    burst_done_ = true;
    burst(burst_count);
  }
  if (!emitting || rate <= 0.0f) return;

  rate_accumulator_ += static_cast<double>(rate) * dt;
  // Small epsilon so e.g. 10/s over 60 steps of 1/60 s yields exactly 10 despite rounding.
  const int due = static_cast<int>(std::floor(rate_accumulator_ + 1e-6));
  if (due > 0) {
    rate_accumulator_ = std::max(0.0, rate_accumulator_ - due);
    burst(due);  // spawns beyond max_particles are dropped
  }
}

UvRect ParticleEmitter::frame_uv() const noexcept {
  const int cols = std::max(1, atlas_columns), rows = std::max(1, atlas_rows);
  const int f = std::clamp(frame, 0, cols * rows - 1);
  const float w = 1.0f / static_cast<float>(cols), h = 1.0f / static_cast<float>(rows);
  const float u = static_cast<float>(f % cols) * w, v = static_cast<float>(f / cols) * h;
  return UvRect{u, v, u + w, v + h};
}

void register_particle_components(ComponentRegistry& registry) {
  using C = ParticleEmitter;
  registry.register_component(
      "ParticleEmitter", [] { return std::make_unique<C>(); },
      {
          make_property<C>("emitting", PropertyType::Bool, &C::emitting),
          make_property<C>("rate", PropertyType::Float, &C::rate),
          make_property<C>("burst_count", PropertyType::Int, &C::burst_count),
          make_property<C>("lifetime_min", PropertyType::Float, &C::lifetime_min),
          make_property<C>("lifetime_max", PropertyType::Float, &C::lifetime_max),
          make_property<C>("speed_min", PropertyType::Float, &C::speed_min),
          make_property<C>("speed_max", PropertyType::Float, &C::speed_max),
          make_property<C>("angle_min", PropertyType::Float, &C::angle_min),
          make_property<C>("angle_max", PropertyType::Float, &C::angle_max),
          make_property<C>("gravity", PropertyType::Vec2, &C::gravity),
          make_property<C>("start_color", PropertyType::Color, &C::start_color),
          make_property<C>("end_color", PropertyType::Color, &C::end_color),
          make_property<C>("start_size", PropertyType::Float, &C::start_size),
          make_property<C>("end_size", PropertyType::Float, &C::end_size),
          make_property<C>("texture", PropertyType::AssetId, &C::texture),
          make_property<C>("atlas_columns", PropertyType::Int, &C::atlas_columns),
          make_property<C>("atlas_rows", PropertyType::Int, &C::atlas_rows),
          make_property<C>("frame", PropertyType::Int, &C::frame),
          make_property<C>("world_space", PropertyType::Bool, &C::world_space),
          make_property<C>("max_particles", PropertyType::Int, &C::max_particles),
          make_property<C>("seed", PropertyType::Int, &C::seed),
      });
}

}  // namespace arcane
