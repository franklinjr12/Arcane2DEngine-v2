#include "arcane/post_process.hpp"

#include <glad/gl.h>

#include "arcane/display.hpp"

namespace arcane {

struct PostProcessChain::Impl {
  GLuint textures[2] = {0, 0};
  GLuint fbos[2] = {0, 0};
  GLuint vao = 0, vbo = 0;
  int width = 0, height = 0;
  GLuint saved_framebuffer = 0;
  bool active = false;
  bool has_letterbox = false;
  DisplayViewport letterbox;

  ~Impl() {
    if (fbos[0]) glDeleteFramebuffers(2, fbos);
    if (textures[0]) glDeleteTextures(2, textures);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
  }

  void ensure(int w, int h) {
    if (!vao) {
      // Triangle strip over clip space; uv 0..1; white color (the sprite vertex layout: pos, uv, color).
      static constexpr float kQuad[4][8] = {{-1, -1, 0, 0, 1, 1, 1, 1}, {1, -1, 1, 0, 1, 1, 1, 1},
                                            {-1, 1, 0, 1, 1, 1, 1, 1},  {1, 1, 1, 1, 1, 1, 1, 1}};
      glGenVertexArrays(1, &vao);
      glGenBuffers(1, &vbo);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
      glBindVertexArray(0);
    }
    if (!fbos[0]) {
      glGenTextures(2, textures);
      glGenFramebuffers(2, fbos);
    }
    if (w == width && h == height) return;
    width = w;
    height = h;
    for (int i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void draw_quad(GLuint source_texture) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
  }
};

PostProcessChain::PostProcessChain() : impl_(std::make_unique<Impl>()) {}
PostProcessChain::PostProcessChain(PostProcessChain&& other) noexcept = default;
PostProcessChain& PostProcessChain::operator=(PostProcessChain&& other) noexcept = default;
PostProcessChain::~PostProcessChain() = default;

void PostProcessChain::begin(int width, int height, const DisplayViewport* letterbox) {
  Impl& m = *impl_;
  GLint bound = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
  m.saved_framebuffer = static_cast<GLuint>(bound);
  m.ensure(width, height);
  m.has_letterbox = letterbox != nullptr;
  if (letterbox) m.letterbox = *letterbox;
  m.active = true;
  glBindFramebuffer(GL_FRAMEBUFFER, m.fbos[0]);
}

void PostProcessChain::apply(std::span<const MaterialShader* const> passes, float time) {
  Impl& m = *impl_;
  if (!m.active) return;
  m.active = false;
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, m.width, m.height);
  glDisable(GL_BLEND);

  int source = 0;  // index of the texture holding the previous pass
  for (std::size_t i = 0; i + 1 < passes.size(); ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, m.fbos[1 - source]);
    passes[i]->bind_fullscreen(time, m.width, m.height);
    m.draw_quad(m.textures[source]);
    source = 1 - source;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, m.saved_framebuffer);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (m.has_letterbox) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(m.letterbox.x, m.letterbox.y, m.letterbox.width, m.letterbox.height);
  }
  if (passes.empty()) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m.fbos[source]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m.saved_framebuffer);
    glBlitFramebuffer(0, 0, m.width, m.height, 0, 0, m.width, m.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m.saved_framebuffer);
  } else {
    const MaterialShader& last = *passes.back();
    last.bind_fullscreen(time, m.width, m.height);
    switch (last.blend()) {
      case MaterialBlend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
      case MaterialBlend::Additive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
      case MaterialBlend::Opaque: break;  // blending stays off
    }
    m.draw_quad(m.textures[source]);
  }

  glUseProgram(0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (m.has_letterbox) glViewport(m.letterbox.x, m.letterbox.y, m.letterbox.width, m.letterbox.height);
}

}  // namespace arcane
