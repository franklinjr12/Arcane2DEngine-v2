#pragma once

#include <cstdint>
#include <vector>

#include "arcane/component.hpp"
#include "arcane/export.hpp"
#include "arcane/math2d.hpp"
#include "arcane/property.hpp"
#include "arcane/renderer.hpp"

namespace arcane {

class ComponentRegistry;

// One live particle. Runtime state only; never serialized.
struct Particle {
  Vec2 position;  // world space, or emitter-local when ParticleEmitter::world_space is false
  Vec2 velocity;  // px/s
  float age = 0.0f;
  float lifetime = 1.0f;
};

// CPU particle emitter. Simulated in on_update(fixed_dt), so Play and the player step it at the fixed rate and it
// is deterministic for a given seed (own PRNG, no std distributions). The editor also calls on_update with the
// real frame time to preview it outside Play. Drawn by the editor/player scene renderer through the sprite batch.
// Units: pixels, seconds, y-down. Angles are degrees, 0 = +x, 90 = down on screen, added to the node's rotation.
// Size and color lerp linearly from start to end over each particle's life (alpha is the color's `a`).
// Local-space particles follow the node's position (not rotation or scale).
class ARCANE_API ParticleEmitter : public Component {
 public:
  ParticleEmitter();
  ~ParticleEmitter() override;

  bool emitting = true;
  float rate = 20.0f;            // particles per second
  int burst_count = 0;           // emitted once on the first update
  float lifetime_min = 0.5f;     // seconds
  float lifetime_max = 1.0f;
  float speed_min = 40.0f;       // px/s
  float speed_max = 80.0f;
  float angle_min = 0.0f;        // degrees
  float angle_max = 360.0f;
  Vec2 gravity{0.0f, 0.0f};      // px/s^2
  Color start_color{1.0f, 1.0f, 1.0f, 1.0f};
  Color end_color{1.0f, 1.0f, 1.0f, 0.0f};
  float start_size = 8.0f;       // px
  float end_size = 2.0f;
  AssetId texture = 0;           // 0 = solid white square
  int atlas_columns = 1;         // texture is a regular grid; `frame` picks the cell (0-based, row-major)
  int atlas_rows = 1;
  int frame = 0;
  bool world_space = true;       // false: particles move with the node
  int max_particles = 256;
  int seed = 1;

  void on_update(float dt) override;

  // Emits up to `count` particles now (clamped by max_particles); returns how many were created.
  int burst(int count);
  // Clears all particles and restarts the random sequence, rate accumulator and initial burst.
  void restart();

  const std::vector<Particle>& particles() const noexcept { return particles_; }
  UvRect frame_uv() const noexcept;

 private:
  void spawn();
  float random01();

  std::vector<Particle> particles_;
  double rate_accumulator_ = 0.0;
  std::uint64_t rng_state_ = 0;
  bool rng_seeded_ = false;
  bool burst_done_ = false;
};

ARCANE_API void register_particle_components(ComponentRegistry& registry);

}  // namespace arcane
