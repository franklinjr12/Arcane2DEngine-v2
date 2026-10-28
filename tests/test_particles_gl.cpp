// Renders a particle emitter through editor render_scene into an offscreen FBO and checks pixels.
// Needs a GL context; exits 77 (CTest skip) without one.
#include <glad/gl.h>

#include <cstdio>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/particles.hpp"
#include "arcane/scene.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {
constexpr int kSkipReturnCode = 77;
constexpr int kSize = 64;
}  // namespace

int main() {
  auto window = arcane::Window::create({.width = kSize, .height = kSize, .title = "test_particles_gl", .hidden = true});
  if (!window) {
    std::fprintf(stderr, "no GL-capable display available, skipping: %s\n", window.error().c_str());
    return kSkipReturnCode;
  }
  if (!arcane::load_gl_functions()) {
    std::fprintf(stderr, "failed to load GL functions, skipping\n");
    return kSkipReturnCode;
  }
  auto renderer = arcane::SpriteRenderer::create();
  ARCANE_ASSERT_TRUE(renderer.has_value());

  GLuint color = 0, fbo = 0;
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "offscreen FBO incomplete, skipping\n");
    return kSkipReturnCode;
  }

  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::register_particle_components(registry);
  arcane::Scene scene;
  arcane::Node* cam = scene.create_node("cam");
  scene.add_component(cam, "Camera", registry);
  cam->local_transform().position = {32.0f, 32.0f};  // view = world [0,64)^2
  arcane::Node* fx = scene.create_node("fx");
  fx->local_transform().position = {32.0f, 32.0f};
  auto* e = static_cast<arcane::ParticleEmitter*>(scene.add_component(fx, "ParticleEmitter", registry));
  e->rate = 0.0f;
  e->burst_count = 1;
  e->speed_min = e->speed_max = 0.0f;
  e->lifetime_min = e->lifetime_max = 1.0f;
  e->start_size = e->end_size = 20.0f;
  e->start_color = e->end_color = {1.0f, 0.0f, 0.0f, 1.0f};

  arcane::editor::TextureCache textures;
  auto draw = [&] { arcane::editor::render_scene(scene, *renderer, textures, kSize, kSize); };
  auto at = [&](int x, int y) { return renderer->read_pixel(x, kSize - 1 - y); };

  draw();  // nothing simulated yet: background only
  ARCANE_ASSERT_TRUE(at(32, 32).r < 0.5f);

  arcane::editor::preview_particles(scene, 1.0f / 60.0f);
  draw();
  ARCANE_ASSERT_TRUE(at(32, 32).r > 0.9f && at(32, 32).g < 0.1f);  // red square, white texture tinted
  ARCANE_ASSERT_TRUE(at(32 + 14, 32).r < 0.5f);                    // outside the 20px square
  ARCANE_ASSERT_TRUE(at(32 + 8, 32 + 8).r > 0.9f);                 // inside it

  arcane::editor::restart_particles(scene);
  e->burst_count = 0;
  draw();
  ARCANE_ASSERT_TRUE(at(32, 32).r < 0.5f);  // cleared
  return 0;
}
