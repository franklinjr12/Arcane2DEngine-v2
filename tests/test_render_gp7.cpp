// GP7 render evidence: named cases drawn with render_scene into an offscreen 160x120 framebuffer and compared with
// tests/golden/render/<case>.png. Set ARCANE_UPDATE_GOLDEN=1 to rewrite the goldens. Tolerance: each RGBA channel
// may differ by 2 and at most 0.5% of the pixels (docs/formats/replay.md). Goldens assume Mesa llvmpipe under Xvfb.
// Exits 77 (CTest skip) without a GL display.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/assets.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/material.hpp"
#include "arcane/renderer.hpp"
#include "arcane/scene.hpp"
#include "arcane/screenshot.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {
constexpr int kSkipReturnCode = 77;
constexpr int kWidth = 160, kHeight = 120;
constexpr int kChannelTolerance = 2;
constexpr double kMaxDifferingFraction = 0.005;
constexpr arcane::AssetId kWhite = 1;

struct Env {
  arcane::SpriteRenderer& renderer;
  arcane::editor::TextureCache& textures;
  arcane::ComponentRegistry registry;
  arcane::editor::MaterialCache materials;
  std::filesystem::path project_dir;  // temporary project holding the material files
};

arcane::Node* add_sprite(arcane::Scene& scene, Env& env, const char* name, float x, float y, float size, arcane::Color tint) {
  arcane::Node* node = scene.create_node(name);
  node->local_transform().position = {x, y};
  auto* sprite = static_cast<arcane::Sprite*>(scene.add_component(node, "Sprite", env.registry));
  sprite->texture = kWhite;
  sprite->width = size;
  sprite->height = size;
  sprite->tint = tint;
  return node;
}

void set_order(arcane::Scene& scene, Env& env, arcane::Node* node, int layer, int order, bool y_sort = false) {
  auto* ro = static_cast<arcane::RenderOrder*>(scene.add_component(node, "RenderOrder", env.registry));
  ro->layer = layer;
  ro->order = order;
  ro->y_sort = y_sort;
}

// Left: red (layer 1) is listed first but covers green and blue (layer 0). Right: three y-sorted squares listed
// top-to-bottom reversed, so the lowest one (largest y) must end up on top.
void build_layers(arcane::Scene& scene, Env& env) {
  using arcane::Color;
  arcane::Node* red = add_sprite(scene, env, "red", -40, 0, 40, Color{1, 0, 0, 1});
  set_order(scene, env, red, 1, 0);
  add_sprite(scene, env, "green", -55, -10, 40, Color{0, 1, 0, 1});
  add_sprite(scene, env, "blue", -25, 10, 40, Color{0, 0, 1, 1});

  set_order(scene, env, add_sprite(scene, env, "y_low", 40, 20, 36, Color{1, 1, 0, 1}), 0, 0, true);
  set_order(scene, env, add_sprite(scene, env, "y_mid", 55, 0, 36, Color{0, 1, 1, 1}), 0, 0, true);
  set_order(scene, env, add_sprite(scene, env, "y_high", 70, -20, 36, Color{1, 0, 1, 1}), 0, 0, true);
}

void write_file(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

arcane::AssetId write_material(Env& env, const char* name, const std::string& frag, arcane::Material material) {
  material.shader = std::string("assets/") + name + ".frag";
  write_file(env.project_dir / material.shader, frag);
  auto json = arcane::save_material_canonical_json(material);
  ARCANE_ASSERT_TRUE(json.has_value());
  const std::string relative = std::string(name) + ".material.json";
  write_file(env.project_dir / "assets" / relative, *json);
  return arcane::asset_id_for_relative_path(relative);
}

void use_material(arcane::Scene& scene, Env& env, arcane::Node* node, arcane::AssetId id) {
  static_cast<arcane::SpriteMaterial*>(scene.add_component(node, "SpriteMaterial", env.registry))->material = id;
}

// Top row: plain gray, white with a vec4 'stripe_color' stripe material, plain gray again (the material must not
// leak into the next sprite). Bottom row: a blue square with a half-covering sprite on top using an additive
// material, next to the same two sprites with the built-in shader for comparison.
void build_material(arcane::Scene& scene, Env& env) {
  using arcane::Color;
  const arcane::AssetId stripes = write_material(env, "stripes", R"(
void main() {
  vec4 base = texture(u_texture, v_uv) * v_color;
  float s = step(0.5, fract(v_uv.x * 4.0));
  frag_color = mix(base, stripe_color, s);
}
)", arcane::Material{.params = {{"stripe_color", {1.0f, 0.2f, 0.1f, 1.0f}}}});
  const arcane::AssetId glow = write_material(env, "glow", R"(
void main() {
  frag_color = vec4(glow_color, 1.0) * texture(u_texture, v_uv).a;
}
)", arcane::Material{.blend = arcane::MaterialBlend::Additive, .params = {{"glow_color", {0.0f, 0.5f, 0.0f}}}});
  env.materials.set_project_dir(env.project_dir);
  env.materials.set_assets(arcane::list_animation_clip_assets(env.project_dir / "assets"));

  add_sprite(scene, env, "plain", -50, -30, 36, Color{0.6f, 0.6f, 0.6f, 1});
  use_material(scene, env, add_sprite(scene, env, "striped", 0, -30, 36, Color{1, 1, 1, 1}), stripes);
  add_sprite(scene, env, "plain_after", 50, -30, 36, Color{0.6f, 0.6f, 0.6f, 1});

  add_sprite(scene, env, "blue", -30, 30, 36, Color{0, 0, 1, 1});
  use_material(scene, env, add_sprite(scene, env, "additive", -15, 40, 36, Color{1, 1, 1, 1}), glow);
  add_sprite(scene, env, "blue_ref", 40, 30, 36, Color{0, 0, 1, 1});
  add_sprite(scene, env, "plain_ref", 55, 40, 36, Color{0, 0.5f, 0, 1});
}

void add_post_process(arcane::Scene& scene, Env& env, const char* name, arcane::AssetId material, int order, bool enabled = true) {
  auto* pass = static_cast<arcane::PostProcess*>(scene.add_component(scene.create_node(name), "PostProcess", env.registry));
  pass->material = material;
  pass->order = order;
  pass->enabled = enabled;
}

// Colored sprites plus a chain whose scene order differs from its `order`: invert (order 1) runs before the
// edge-darkening uv-shift pass (order 5, listed first in the scene); a disabled grayscale pass must be skipped.
// Inverting after the darkening would give light edges instead of dark ones.
void build_postprocess(arcane::Scene& scene, Env& env) {
  using arcane::Color;
  const arcane::AssetId invert = write_material(env, "invert", R"(
void main() {
  vec4 c = texture(u_texture, v_uv) * v_color;
  frag_color = vec4(1.0 - c.rgb, c.a);
}
)", arcane::Material{});
  const arcane::AssetId shift = write_material(env, "shift", R"(
void main() {
  vec2 d = v_uv - vec2(0.5);
  float vignette = 1.0 - dot(d, d) * strength;
  frag_color = vec4(texture(u_texture, v_uv + vec2(offset, 0.0)).rgb * vignette, 1.0);
}
)", arcane::Material{.params = {{"offset", {0.1f}}, {"strength", {2.0f}}}});
  const arcane::AssetId gray = write_material(env, "gray", R"(
void main() {
  float l = dot(texture(u_texture, v_uv).rgb, vec3(0.3, 0.6, 0.1));
  frag_color = vec4(l, l, l, 1.0);
}
)", arcane::Material{});
  env.materials.set_project_dir(env.project_dir);
  env.materials.set_assets(arcane::list_animation_clip_assets(env.project_dir / "assets"));

  add_sprite(scene, env, "red", -50, -25, 40, Color{1, 0, 0, 1});
  add_sprite(scene, env, "green", 0, 10, 40, Color{0, 1, 0, 1});
  add_sprite(scene, env, "blue", 50, -25, 40, Color{0, 0, 1, 1});
  add_sprite(scene, env, "yellow", 50, 35, 30, Color{1, 1, 0, 1});
  add_post_process(scene, env, "shift_pass", shift, 5);
  add_post_process(scene, env, "gray_off", gray, 3, false);
  add_post_process(scene, env, "invert_pass", invert, 1);
}

struct Case {
  const char* name;
  std::function<void(arcane::Scene&, Env&)> build;
};

const std::vector<Case>& cases() {
  static const std::vector<Case> list = {{"layers", build_layers}, {"material", build_material}, {"postprocess", build_postprocess}};
  return list;
}
}  // namespace

int main() {
  auto window = arcane::Window::create({.width = kWidth, .height = kHeight, .title = "test_render_gp7", .hidden = true});
  if (!window || !arcane::load_gl_functions()) {
    std::fprintf(stderr, "no GL-capable display available, skipping\n");
    return kSkipReturnCode;
  }
  auto renderer = arcane::SpriteRenderer::create();
  ARCANE_ASSERT_TRUE(renderer.has_value());
  arcane::editor::TextureCache textures;
  const unsigned char white[4] = {255, 255, 255, 255};
  auto white_texture = arcane::Texture::load_from_pixels(white, 1, 1, arcane::TextureFilter::Nearest);
  ARCANE_ASSERT_TRUE(white_texture.has_value());
  textures.put(kWhite, std::move(*white_texture));

  Env env{*renderer, textures, {}, {}, std::filesystem::temp_directory_path() / "arcane_test_render_gp7"};
  std::filesystem::remove_all(env.project_dir);
  arcane::register_builtin_components(env.registry);

  // A fragment body with a syntax error reports the GLSL log instead of a shader.
  auto broken = arcane::MaterialShader::compile("void main( {", arcane::Material{});
  ARCANE_ASSERT_TRUE(!broken.has_value());
  ARCANE_ASSERT_TRUE(!broken.error().empty());
  auto missing_param = arcane::MaterialShader::compile("void main() { frag_color = vec4(nope); }", arcane::Material{});
  ARCANE_ASSERT_TRUE(!missing_param.has_value());

  const bool update = std::getenv("ARCANE_UPDATE_GOLDEN") != nullptr;
  for (const Case& c : cases()) {
    arcane::Scene scene;
    c.build(scene, env);
    auto image = arcane::render_offscreen(kWidth, kHeight, [&] {
      arcane::editor::render_scene(scene, env.renderer, env.textures, kWidth, kHeight, nullptr, nullptr, nullptr, nullptr, true,
                                        nullptr, nullptr, &env.materials);
    });
    ARCANE_ASSERT_TRUE(image.has_value());
    const auto golden = std::filesystem::path(ARCANE_GOLDEN_DIR) / "render" / (std::string(c.name) + ".png");
    if (update) {
      ARCANE_ASSERT_TRUE(arcane::write_png(golden, *image).has_value());
      std::printf("golden updated: %s\n", c.name);
      continue;
    }
    auto expected = arcane::read_png(golden);
    if (!expected) {
      std::fprintf(stderr, "missing golden %s (run with ARCANE_UPDATE_GOLDEN=1)\n", golden.string().c_str());
      return 1;
    }
    const arcane::ImageDiff diff = arcane::compare_images(*image, *expected, kChannelTolerance);
    std::printf("%s: size_match=%d max_delta=%d differing=%ld\n", c.name, diff.same_size, diff.max_channel_delta,
                diff.differing_pixels);
    ARCANE_ASSERT_TRUE(diff.same_size);
    ARCANE_ASSERT_TRUE(static_cast<double>(diff.differing_pixels) <= kMaxDifferingFraction * kWidth * kHeight);
  }
  std::filesystem::remove_all(env.project_dir);
  return 0;
}
