#include "arcane/renderer.hpp"

#include "arcane/display.hpp"

#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <vector>

#include "arcane/log.hpp"

namespace arcane {

namespace {
constexpr std::string_view kLogCategory = "renderer";

GLenum gl_filter(TextureFilter filter) { return filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR; }

std::uint32_t make_gl_texture(const unsigned char* rgba, int width, int height, TextureFilter filter) {
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter(filter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter(filter));
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindTexture(GL_TEXTURE_2D, 0);
  return id;
}

const char* kVertexShaderSource = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform mat4 u_view_projection;
out vec2 v_uv;
out vec4 v_color;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = u_view_projection * vec4(a_pos, 0.0, 1.0);
}
)glsl";

const char* kFragmentShaderSource = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_texture;
out vec4 frag_color;
void main() {
  frag_color = texture(u_texture, v_uv) * v_color;
}
)glsl";

std::expected<GLuint, std::string> compile_shader(GLenum stage, const char* source) {
  GLuint shader = glCreateShader(stage);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log_buffer[1024];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, sizeof(log_buffer), &len, log_buffer);
    glDeleteShader(shader);
    return std::unexpected(std::string(log_buffer, static_cast<size_t>(len)));
  }
  return shader;
}

std::expected<GLuint, std::string> link_program(GLuint vertex, GLuint fragment) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log_buffer[1024];
    GLsizei len = 0;
    glGetProgramInfoLog(program, sizeof(log_buffer), &len, log_buffer);
    glDeleteProgram(program);
    return std::unexpected(std::string(log_buffer, static_cast<size_t>(len)));
  }
  return program;
}

struct Vertex {
  float x, y;
  float u, v;
  float r, g, b, a;
};

// Column-major 4x4 orthographic projection * view, matching GLSL's mat4 layout.
std::array<float, 16> make_view_projection(float cam_x, float cam_y, float zoom, float viewport_w,
                                            float viewport_h) {
  const float half_w = (viewport_w / std::max(zoom, 0.0001f)) * 0.5f;
  const float half_h = (viewport_h / std::max(zoom, 0.0001f)) * 0.5f;
  const float left = cam_x - half_w, right = cam_x + half_w;
  const float bottom = cam_y + half_h, top = cam_y - half_h;
  const float near = -1.0f, far = 1.0f;

  std::array<float, 16> m{};
  m[0] = 2.0f / (right - left);
  m[5] = 2.0f / (top - bottom);
  m[10] = -2.0f / (far - near);
  m[12] = -(right + left) / (right - left);
  m[13] = -(top + bottom) / (top - bottom);
  m[14] = -(far + near) / (far - near);
  m[15] = 1.0f;
  return m;
}
}  // namespace

bool sprite_in_view(const SpriteDraw& sprite, float cam_x, float cam_y, float zoom, float view_width,
                    float view_height) noexcept {
  const float z = std::max(zoom, 0.0001f);
  const float half_w = (view_width / z) * 0.5f;
  const float half_h = (view_height / z) * 0.5f;
  const float ox = sprite.origin_x * sprite.width;
  const float oy = sprite.origin_y * sprite.height;
  // Corner offsets from the pivot (x, y), before rotation.
  const float x0 = std::min(-ox, sprite.width - ox), x1 = std::max(-ox, sprite.width - ox);
  const float y0 = std::min(-oy, sprite.height - oy), y1 = std::max(-oy, sprite.height - oy);
  float min_x, max_x, min_y, max_y;
  if (sprite.rotation_radians == 0.0f) {
    min_x = sprite.x + x0;
    max_x = sprite.x + x1;
    min_y = sprite.y + y0;
    max_y = sprite.y + y1;
  } else {
    const float r = std::sqrt(std::max(x0 * x0, x1 * x1) + std::max(y0 * y0, y1 * y1));
    min_x = sprite.x - r;
    max_x = sprite.x + r;
    min_y = sprite.y - r;
    max_y = sprite.y + r;
  }
  return max_x >= cam_x - half_w && min_x <= cam_x + half_w && max_y >= cam_y - half_h &&
         min_y <= cam_y + half_h;
}

std::expected<Texture, std::string> Texture::load_from_pixels(std::span<const unsigned char> rgba_pixels,
                                                                int width, int height, TextureFilter filter) {
  if (static_cast<size_t>(width) * static_cast<size_t>(height) * 4 > rgba_pixels.size()) {
    return std::unexpected("pixel buffer too small for given width/height");
  }
  Texture texture;
  texture.id_ = make_gl_texture(rgba_pixels.data(), width, height, filter);
  texture.width_ = width;
  texture.height_ = height;
  return texture;
}

std::expected<Texture, std::string> Texture::load_from_memory(std::span<const unsigned char> encoded_bytes,
                                                                 TextureFilter filter) {
  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels =
      stbi_load_from_memory(encoded_bytes.data(), static_cast<int>(encoded_bytes.size()), &width, &height,
                             &channels, 4);
  if (!pixels) {
    return std::unexpected(std::format("stb_image decode failed: {}", stbi_failure_reason()));
  }
  auto result = load_from_pixels(std::span(pixels, static_cast<size_t>(width) * height * 4), width, height, filter);
  stbi_image_free(pixels);
  return result;
}

std::expected<Texture, std::string> Texture::load_from_file(const std::filesystem::path& path,
                                                               TextureFilter filter) {
  // Open the native path ourselves: stb's narrow filename API uses the
  // Windows ANSI code page and cannot open arbitrary Unicode filenames.
#ifdef _WIN32
  FILE* file = _wfopen(path.c_str(), L"rb");
#else
  FILE* file = std::fopen(path.c_str(), "rb");
#endif
  if (!file) return std::unexpected("failed to open texture file");
  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_file(file, &width, &height, &channels, 4);
  std::fclose(file);
  if (!pixels) {
    return std::unexpected(std::format("stb_image decode failed: {}", stbi_failure_reason()));
  }
  auto result = load_from_pixels(std::span(pixels, static_cast<size_t>(width) * height * 4), width, height, filter);
  stbi_image_free(pixels);
  return result;
}

Texture::Texture(Texture&& other) noexcept : id_(other.id_), width_(other.width_), height_(other.height_) {
  other.id_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
  if (this != &other) {
    if (id_) {
      glDeleteTextures(1, &id_);
    }
    id_ = other.id_;
    width_ = other.width_;
    height_ = other.height_;
    other.id_ = 0;
  }
  return *this;
}

Texture::~Texture() {
  if (id_) {
    glDeleteTextures(1, &id_);
  }
}

struct SpriteRenderer::Impl {
  GLuint program = 0;
  GLint view_projection_location = -1;
  GLuint vao = 0, vbo = 0, ebo = 0;
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;  // static 0,1,2,2,3,0 pattern for index_quads quads
  std::size_t index_quads = 0;         // quads whose indices are uploaded to the EBO
  std::size_t vbo_capacity = 0;        // bytes allocated for the VBO
  const Texture* batch_texture = nullptr;
  float cam_x = 0.0f, cam_y = 0.0f, cam_zoom = 1.0f;
  int fb_width = 1, fb_height = 1;
  bool culling = true;
  RenderStats stats;

  ~Impl() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (program) glDeleteProgram(program);
  }

  // Grows the static index buffer (EBO bound through the VAO) to hold `quads` quads.
  void ensure_indices(std::size_t quads) {
    if (quads <= index_quads) return;
    std::size_t capacity = std::max<std::size_t>(index_quads, 256);
    while (capacity < quads) capacity *= 2;
    capacity = std::min<std::size_t>(capacity, kSpriteMaxBatchQuads);
    indices.resize(capacity * 6);
    for (std::size_t q = index_quads; q < capacity; ++q) {
      const auto base = static_cast<std::uint32_t>(q * 4);
      std::uint32_t* out = &indices[q * 6];
      out[0] = base; out[1] = base + 1; out[2] = base + 2;
      out[3] = base + 2; out[4] = base + 3; out[5] = base;
    }
    index_quads = capacity;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
  }

  void flush() {
    if (vertices.empty() || !batch_texture) {
      return;
    }
    const std::size_t quads = vertices.size() / 4;
    const std::size_t bytes = vertices.size() * sizeof(Vertex);
    glBindVertexArray(vao);
    ensure_indices(quads);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (bytes > vbo_capacity) {
      vbo_capacity = std::max<std::size_t>(bytes, vbo_capacity * 2);
    }
    // Orphan (or grow) the buffer so the driver need not wait on the previous draw.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vbo_capacity), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices.data());

    glUseProgram(program);
    auto vp = make_view_projection(cam_x, cam_y, cam_zoom, static_cast<float>(fb_width),
                                    static_cast<float>(fb_height));
    glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, vp.data());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batch_texture->native_id());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quads * 6), GL_UNSIGNED_INT, nullptr);
    ++stats.draw_calls;

    vertices.clear();
  }
};

std::expected<SpriteRenderer, std::string> SpriteRenderer::create() {
  auto vertex_shader = compile_shader(GL_VERTEX_SHADER, kVertexShaderSource);
  if (!vertex_shader) return std::unexpected(vertex_shader.error());
  auto fragment_shader = compile_shader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
  if (!fragment_shader) {
    glDeleteShader(*vertex_shader);
    return std::unexpected(fragment_shader.error());
  }
  auto program = link_program(*vertex_shader, *fragment_shader);
  glDeleteShader(*vertex_shader);
  glDeleteShader(*fragment_shader);
  if (!program) return std::unexpected(program.error());

  SpriteRenderer renderer;
  renderer.impl_ = std::make_unique<Impl>();
  renderer.impl_->vertices.reserve(4096 * 4);
  renderer.impl_->program = *program;
  renderer.impl_->view_projection_location = glGetUniformLocation(*program, "u_view_projection");

  glGenVertexArrays(1, &renderer.impl_->vao);
  glGenBuffers(1, &renderer.impl_->vbo);
  glGenBuffers(1, &renderer.impl_->ebo);
  glBindVertexArray(renderer.impl_->vao);
  glBindBuffer(GL_ARRAY_BUFFER, renderer.impl_->vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer.impl_->ebo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
  glBindVertexArray(0);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ARCANE_LOG_INFO(kLogCategory, "sprite renderer ready");
  return renderer;
}

SpriteRenderer::SpriteRenderer(SpriteRenderer&& other) noexcept : impl_(std::move(other.impl_)) {}

SpriteRenderer& SpriteRenderer::operator=(SpriteRenderer&& other) noexcept {
  if (this != &other) {
    impl_ = std::move(other.impl_);
  }
  return *this;
}

SpriteRenderer::SpriteRenderer() = default;
SpriteRenderer::~SpriteRenderer() = default;

void SpriteRenderer::set_camera(float x, float y, float zoom) {
  impl_->cam_x = x;
  impl_->cam_y = y;
  impl_->cam_zoom = zoom;
}

void SpriteRenderer::begin_frame(int framebuffer_width, int framebuffer_height, const Color& clear_color) {
  impl_->fb_width = framebuffer_width;
  impl_->fb_height = framebuffer_height;
  impl_->stats = RenderStats{};
  impl_->batch_texture = nullptr;
  impl_->vertices.clear();
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, framebuffer_width, framebuffer_height);
  glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SpriteRenderer::begin_frame(int framebuffer_width, int framebuffer_height, const DisplayViewport& viewport,
                                 const Color& clear_color) {
  impl_->fb_width = viewport.logical_width;
  impl_->fb_height = viewport.logical_height;
  impl_->stats = RenderStats{};
  impl_->batch_texture = nullptr;
  impl_->vertices.clear();
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, framebuffer_width, framebuffer_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
  glEnable(GL_SCISSOR_TEST);  // sprites and the HUD never bleed into the bars
  glScissor(viewport.x, viewport.y, viewport.width, viewport.height);
  glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SpriteRenderer::draw(const SpriteDraw& sprite) {
  if (!sprite.texture) {
    return;
  }
  if (impl_->culling && !sprite_in_view(sprite, impl_->cam_x, impl_->cam_y, impl_->cam_zoom,
                                         static_cast<float>(impl_->fb_width),
                                         static_cast<float>(impl_->fb_height))) {
    ++impl_->stats.culled;
    return;
  }
  if (impl_->batch_texture && (impl_->batch_texture != sprite.texture ||
                               impl_->vertices.size() >= static_cast<std::size_t>(kSpriteMaxBatchQuads) * 4)) {
    impl_->flush();
  }
  impl_->batch_texture = sprite.texture;
  ++impl_->stats.sprites;

  const float ox = sprite.origin_x * sprite.width;
  const float oy = sprite.origin_y * sprite.height;
  const float local[4][2] = {
      {-ox, -oy}, {sprite.width - ox, -oy}, {sprite.width - ox, sprite.height - oy}, {-ox, sprite.height - oy}};
  const bool rotated = sprite.rotation_radians != 0.0f;
  const float cos_r = rotated ? std::cos(sprite.rotation_radians) : 1.0f;
  const float sin_r = rotated ? std::sin(sprite.rotation_radians) : 0.0f;
  const float uvs[4][2] = {{sprite.uv.u0, sprite.uv.v0},
                            {sprite.uv.u1, sprite.uv.v0},
                            {sprite.uv.u1, sprite.uv.v1},
                            {sprite.uv.u0, sprite.uv.v1}};

  const std::size_t first = impl_->vertices.size();
  impl_->vertices.resize(first + 4);
  Vertex* out = &impl_->vertices[first];
  for (int i = 0; i < 4; ++i) {
    const float lx = local[i][0], ly = local[i][1];
    Vertex& v = out[i];
    if (rotated) {
      v.x = sprite.x + lx * cos_r - ly * sin_r;
      v.y = sprite.y + lx * sin_r + ly * cos_r;
    } else {
      v.x = sprite.x + lx;
      v.y = sprite.y + ly;
    }
    v.u = uvs[i][0];
    v.v = uvs[i][1];
    v.r = sprite.tint.r;
    v.g = sprite.tint.g;
    v.b = sprite.tint.b;
    v.a = sprite.tint.a;
  }
}

void SpriteRenderer::set_culling(bool enabled) { impl_->culling = enabled; }

void SpriteRenderer::flush() { impl_->flush(); }

const RenderStats& SpriteRenderer::stats() const noexcept { return impl_->stats; }

void SpriteRenderer::end_frame() { impl_->flush(); }

Color SpriteRenderer::read_pixel(int x, int y) const {
  float rgba[4] = {0, 0, 0, 0};
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, rgba);
  return Color{rgba[0], rgba[1], rgba[2], rgba[3]};
}

}  // namespace arcane
