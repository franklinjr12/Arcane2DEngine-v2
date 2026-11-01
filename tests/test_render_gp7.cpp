// GP7 render evidence: named cases drawn with render_scene into an offscreen 160x120 framebuffer and compared with
// tests/golden/render/<case>.png. Set ARCANE_UPDATE_GOLDEN=1 to rewrite the goldens. Tolerance: each RGBA channel
// may differ by 2 and at most 0.5% of the pixels (docs/formats/replay.md). Goldens assume Mesa llvmpipe under Xvfb.
// Exits 77 (CTest skip) without a GL display.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
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

struct Case {
  const char* name;
  std::function<void(arcane::Scene&, Env&)> build;
};

const std::vector<Case>& cases() {
  static const std::vector<Case> list = {{"layers", build_layers}};
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

  Env env{*renderer, textures, {}};
  arcane::register_builtin_components(env.registry);

  const bool update = std::getenv("ARCANE_UPDATE_GOLDEN") != nullptr;
  for (const Case& c : cases()) {
    arcane::Scene scene;
    c.build(scene, env);
    auto image = arcane::render_offscreen(kWidth, kHeight, [&] {
      arcane::editor::render_scene(scene, env.renderer, env.textures, kWidth, kHeight);
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
  return 0;
}
