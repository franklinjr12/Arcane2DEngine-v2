#include "arcane_editor/render_bench.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>

#include "arcane/builtin_components.hpp"
#include "arcane/particles.hpp"
#include "arcane/physics.hpp"
#include "arcane/renderer.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/scene_render.hpp"

namespace arcane::editor {

std::expected<RenderBenchResult, std::string> run_render_benchmark(const BenchConfig& c, int width, int height) {
  if (auto ok = validate_bench_config(c); !ok) return std::unexpected(ok.error());
  if (width < 1 || width > 8192 || height < 1 || height > 8192) {
    return std::unexpected(std::string("render size must be 1..8192 per side"));
  }

  auto window = Window::create({.width = width, .height = height, .title = "arcane render bench", .vsync = false, .hidden = true});
  if (!window) return std::unexpected("no GL-capable display: " + window.error());
  if (!load_gl_functions()) return std::unexpected(std::string("no GL-capable display: GL functions did not load"));
  auto renderer = SpriteRenderer::create();
  if (!renderer) return std::unexpected("renderer setup failed: " + renderer.error());

  GLuint fbo = 0, color = 0;
  glGenFramebuffers(1, &fbo);
  glGenRenderbuffers(1, &color);
  glBindRenderbuffer(GL_RENDERBUFFER, color);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
  const auto release = [&] {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &color);
  };
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    release();
    return std::unexpected(std::string("offscreen framebuffer is incomplete"));
  }

  ComponentRegistry registry;
  register_builtin_components(registry);
  register_physics_components(registry);
  register_particle_components(registry);
  Scene scene;
  if (auto built = build_benchmark_scene(scene, registry, c); !built) {
    release();
    return std::unexpected(built.error());
  }
  // Sprite nodes carry texture 0 (render_scene skips those); point them at a 1x1 white texture registered in the cache.
  // Particles use the cache's own white texture, so a frame is a small, fixed number of batches.
  constexpr AssetId kBenchTexture = 1;
  TextureCache textures;
  const unsigned char white[4] = {255, 255, 255, 255};
  auto bench_texture = Texture::load_from_pixels(white, 1, 1, TextureFilter::Nearest);
  if (!bench_texture) {
    release();
    return std::unexpected("texture setup failed: " + bench_texture.error());
  }
  textures.put(kBenchTexture, std::move(*bench_texture));
  for (const auto& node : scene.roots()) {  // the benchmark scene is flat
    if (auto* sprite = static_cast<Sprite*>(node->find_component("Sprite"))) sprite->texture = kBenchTexture;
  }
  // The sprite grid is 1600 px wide: a view at its top-left leaves the rest outside, so culling is exercised.
  Node* camera = scene.create_node("camera");
  camera->local_transform().position = {static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f};
  scene.add_component(camera, "Camera", registry);

  PhysicsWorld physics;
  physics.rebuild(scene);

  using Clock = std::chrono::steady_clock;
  const auto ms_since = [](Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
  };
  RenderBenchResult result;
  result.config = c;
  result.width = width;
  result.height = height;
  constexpr float kDt = 1.0f / 60.0f;
  double update_total = 0.0, render_total = 0.0;
  for (int frame = 0; frame < c.frames; ++frame) {
    const auto update_start = Clock::now();
    scene.update(kDt);
    physics.step(scene, kDt);
    update_total += ms_since(update_start);

    const auto render_start = Clock::now();
    render_scene(scene, *renderer, textures, width, height);
    glFinish();
    const double render_ms = ms_since(render_start);
    render_total += render_ms;
    result.render_max_frame_ms = std::max(result.render_max_frame_ms, render_ms);
  }
  result.update_avg_ms = update_total / c.frames;
  result.render_avg_frame_ms = render_total / c.frames;
  const RenderStats& stats = renderer->stats();
  result.draw_calls = stats.draw_calls;
  result.sprites = stats.sprites;
  result.culled = stats.culled;
  result.live_particles = count_particles(scene);
  result.physics_bodies = physics.body_count();
  release();
  return result;
}

}  // namespace arcane::editor
