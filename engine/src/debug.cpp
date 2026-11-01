#include "arcane/debug.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "arcane/builtin_components.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"

namespace arcane {

void FrameTimer::tick(double frame_seconds) noexcept {
  window_seconds_ += frame_seconds;
  ++window_frames_;
  if (window_seconds_ >= 0.5) {
    frame_ms_ = window_seconds_ * 1000.0 / window_frames_;
    fps_ = window_frames_ / window_seconds_;
    window_seconds_ = 0.0;
    window_frames_ = 0;
  }
}

namespace {

int count_particles_in(const std::vector<std::unique_ptr<Node>>& nodes) {
  int total = 0;
  for (const auto& node : nodes) {
    if (auto* emitter = dynamic_cast<ParticleEmitter*>(node->find_component("ParticleEmitter"))) {
      total += static_cast<int>(emitter->particles().size());
    }
    total += count_particles_in(node->children());
  }
  return total;
}

}  // namespace

int count_particles(const Scene& scene) { return count_particles_in(scene.roots()); }

std::string format_debug_overlay(const FrameStats& stats) {
  char buffer[256];
  std::snprintf(buffer, sizeof buffer,
                "FPS %.1f\nFrame %.2f ms\nDraw calls %d\nSprites %d\nBodies %d\nParticles %d", stats.fps,
                stats.frame_ms, stats.draw_calls, stats.sprites, stats.physics_bodies, stats.particles);
  return buffer;
}

void fill_debug_overlay_hud(Hud& hud, const FrameStats& stats, const DebugConfig& config) {
  hud.clear();
  HudText text;
  text.id = "debug_overlay";
  text.text = format_debug_overlay(stats);
  text.anchor = HudAnchor::TopLeft;
  text.offset_x = 8.0f;
  text.offset_y = 8.0f;
  text.font_scale = config.overlay_font_scale;
  hud.add(std::move(text));
}

std::expected<BenchResult, std::string> run_benchmark(const BenchConfig& c) {
  const auto in_range = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
  if (!in_range(c.sprites, 0, 1000000)) return std::unexpected(std::string("sprites must be within 0..1000000"));
  if (!in_range(c.emitters, 0, 100000)) return std::unexpected(std::string("emitters must be within 0..100000"));
  if (!in_range(c.particles_per_emitter, 0, 256)) {
    return std::unexpected(std::string("particles_per_emitter must be within 0..256"));
  }
  if (!in_range(c.bodies, 0, 100000)) return std::unexpected(std::string("bodies must be within 0..100000"));
  if (!in_range(c.frames, 1, 100000)) return std::unexpected(std::string("frames must be within 1..100000"));

  using Clock = std::chrono::steady_clock;
  const auto ms_since = [](Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
  };

  const auto setup_start = Clock::now();
  ComponentRegistry registry;
  register_builtin_components(registry);
  register_physics_components(registry);
  register_particle_components(registry);
  Scene scene;
  for (int i = 0; i < c.sprites; ++i) {
    Node* node = scene.create_node("sprite");
    node->local_transform().position = {static_cast<float>(i % 100) * 16.0f, static_cast<float>(i / 100) * 16.0f};
    scene.add_component(node, "Sprite", registry);
  }
  for (int i = 0; i < c.emitters; ++i) {
    Node* node = scene.create_node("emitter");
    node->local_transform().position = {static_cast<float>(i) * 32.0f, 0.0f};
    auto* emitter = static_cast<ParticleEmitter*>(scene.add_component(node, "ParticleEmitter", registry));
    if (!emitter) return std::unexpected(std::string("ParticleEmitter is not registered"));
    emitter->rate = 0.0f;
    emitter->burst_count = c.particles_per_emitter;
    emitter->max_particles = std::max(1, c.particles_per_emitter);
    emitter->lifetime_min = emitter->lifetime_max = 1000.0f;  // stay alive for the whole run
    emitter->seed = i + 1;
  }
  Node* floor = scene.create_node("floor");
  floor->local_transform().position = {0.0f, 4000.0f};
  auto* floor_body = static_cast<RigidBody2D*>(scene.add_component(floor, "RigidBody2D", registry));
  auto* floor_shape = static_cast<Collider2D*>(scene.add_component(floor, "Collider2D", registry));
  if (!floor_body || !floor_shape) return std::unexpected(std::string("physics components are not registered"));
  floor_body->body_type = static_cast<int>(BodyType::Static);
  floor_shape->width = 100000.0f;
  for (int i = 0; i < c.bodies; ++i) {
    Node* node = scene.create_node("body");
    node->local_transform().position = {static_cast<float>(i % 50) * 40.0f, static_cast<float>(i / 50) * -40.0f};
    scene.add_component(node, "RigidBody2D", registry);
    scene.add_component(node, "Collider2D", registry);
  }
  PhysicsWorld physics;
  physics.rebuild(scene);
  BenchResult result;
  result.config = c;
  result.setup_ms = ms_since(setup_start);

  constexpr float kDt = 1.0f / 60.0f;
  const auto run_start = Clock::now();
  for (int frame = 0; frame < c.frames; ++frame) {
    const auto frame_start = Clock::now();
    scene.update(kDt);
    physics.step(scene, kDt);
    result.max_frame_ms = std::max(result.max_frame_ms, ms_since(frame_start));
  }
  result.total_ms = ms_since(run_start);
  result.avg_frame_ms = result.total_ms / c.frames;
  result.live_particles = count_particles(scene);
  result.physics_bodies = physics.body_count();
  return result;
}

}  // namespace arcane
