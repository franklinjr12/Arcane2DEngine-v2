#pragma once

#include <expected>
#include <string>

#include "arcane/export.hpp"
#include "arcane/hud.hpp"
#include "arcane/scene.hpp"

namespace arcane {

// Debug overlay (FPS, frame time, draw calls, sprites, bodies, particles) and the headless benchmark
// (docs/perf.md). The overlay is a view over DebugConfig (project.json "debug"; edit with debug.get/debug.set).
// Everything here is CPU-only and testable without a window.

// Key that toggles the overlay in the player and editor (GLFW_KEY_F3).
inline constexpr int kDebugOverlayKey = 292;

struct DebugConfig {
  bool overlay_visible = false;  // overlay shown at startup
  float overlay_font_scale = 1.5f;  // HUD font scale, (0, 8]
};

struct FrameStats {
  double fps = 0.0;
  double frame_ms = 0.0;
  int draw_calls = 0;
  int sprites = 0;
  int physics_bodies = 0;
  int particles = 0;
};

// Averages real frame times over ~0.5 s so the numbers are readable.
class ARCANE_API FrameTimer {
 public:
  void tick(double frame_seconds) noexcept;
  double fps() const noexcept { return fps_; }
  double frame_ms() const noexcept { return frame_ms_; }

 private:
  double window_seconds_ = 0.0;
  int window_frames_ = 0;
  double fps_ = 0.0;
  double frame_ms_ = 0.0;
};

// Live particles of every ParticleEmitter in the scene.
ARCANE_API int count_particles(const Scene& scene);

// Six fixed lines: "FPS 60.0", "Frame 16.67 ms", "Draw calls 3", "Sprites 120", "Bodies 5", "Particles 40".
ARCANE_API std::string format_debug_overlay(const FrameStats& stats);

// Replaces the contents of `hud` with the overlay text (top-left, white on the HUD font).
ARCANE_API void fill_debug_overlay_hud(Hud& hud, const FrameStats& stats, const DebugConfig& config);

// Headless benchmark: a scene of `sprites` sprite nodes, `emitters` particle emitters (each bursting
// `particles_per_emitter`) and `bodies` falling dynamic bodies over a static floor, stepped for `frames`
// fixed 60 Hz frames with no window, GL or audio. Rendering is not measured (it needs a GL context).
struct BenchConfig {
  int sprites = 1000;
  int emitters = 10;
  int particles_per_emitter = 100;
  int bodies = 200;
  int frames = 120;
};

struct BenchResult {
  BenchConfig config;
  double setup_ms = 0.0;
  double total_ms = 0.0;
  double avg_frame_ms = 0.0;
  double max_frame_ms = 0.0;
  int live_particles = 0;
  int physics_bodies = 0;
};

// Fails on out-of-range config (counts 0..1000000, frames 1..100000, particles_per_emitter 0..256).
ARCANE_API std::expected<BenchResult, std::string> run_benchmark(const BenchConfig& config);

}  // namespace arcane
