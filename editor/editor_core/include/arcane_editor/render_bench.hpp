#pragma once

#include <expected>
#include <string>

#include "arcane/debug.hpp"

namespace arcane::editor {

// GL render benchmark (docs/perf.md): builds the run_benchmark scene (build_benchmark_scene) plus a primary Camera
// centered on a `width` x `height` view, so the part of the sprite grid outside it is culled. Every frame runs
// scene.update + physics.step and then render_scene into an offscreen framebuffer followed by glFinish, so GPU time is
// included. Needs a GL-capable display (or Xvfb); fails with the window/GL error otherwise.
struct RenderBenchResult {
  BenchConfig config;
  int width = 0;
  int height = 0;
  double render_avg_frame_ms = 0.0;  // render_scene + glFinish
  double render_max_frame_ms = 0.0;
  double update_avg_ms = 0.0;        // scene.update + physics.step
  int draw_calls = 0;                // renderer.stats() of the last frame
  int sprites = 0;
  int culled = 0;
  int live_particles = 0;
  int physics_bodies = 0;
};

std::expected<RenderBenchResult, std::string> run_render_benchmark(const BenchConfig& config, int width = 1280,
                                                                   int height = 720);

}  // namespace arcane::editor
