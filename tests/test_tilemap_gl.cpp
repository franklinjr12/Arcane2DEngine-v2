// Renders a tilemap through editor render_scene into an offscreen FBO and
// checks pixels. Needs a GL context; exits 77 (CTest skip) without one.
#include <glad/gl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "arcane/window.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {

constexpr int kSkipReturnCode = 77;
constexpr int kSize = 64;
constexpr arcane::AssetId kTextureId = 7;

enum class Px { Red, Blue, Background };

Px classify(arcane::Color c) {
  if (c.r > 0.9f && c.g < 0.1f && c.b < 0.1f) return Px::Red;
  if (c.b > 0.9f && c.r < 0.1f && c.g < 0.1f) return Px::Blue;
  return Px::Background;  // the 0.12/0.12/0.16 clear color, or anything unexpected
}

struct Fixture {
  arcane::ComponentRegistry registry;
  arcane::Scene scene;
  arcane::Node* cam = nullptr;
  arcane::Tilemap* map = nullptr;
  arcane::editor::TextureCache textures;
  arcane::editor::TilesetCache tilesets;
  std::filesystem::path dir = std::filesystem::temp_directory_path() / "arcane_test_tilemap_gl";

  // 3x3 map of 16px cells at the origin; tileset = 2 tiles in one row (1 red, 2 blue).
  bool init() {
    arcane::register_builtin_components(registry);
    arcane::register_tilemap_components(registry);
    std::filesystem::create_directories(dir);
    {
      std::ofstream out(dir / "tiles.json");
      arcane::Tileset t;
      t.texture = kTextureId;
      t.tile_width = 1;
      t.tile_height = 1;
      t.columns = 2;
      t.tile_count = 2;
      out << arcane::save_tileset_to_json(t);
    }
    tilesets.set_assets(arcane::editor::list_animation_clip_assets(dir));

    const unsigned char pixels[8] = {255, 0, 0, 255, 0, 0, 255, 255};
    auto texture = arcane::Texture::load_from_pixels(pixels, 2, 1, arcane::TextureFilter::Nearest);
    if (!texture) return false;
    textures.put(kTextureId, std::move(*texture));

    cam = scene.create_node("cam");
    scene.add_component(cam, "Camera", registry);
    arcane::Node* node = scene.create_node("map");
    map = static_cast<arcane::Tilemap*>(scene.add_component(node, "Tilemap", registry));
    map->tileset = arcane::editor::asset_id_for_relative_path("tiles.json");
    map->tile_width = 16.0f;
    map->tile_height = 16.0f;
    map->width = 3;
    map->height = 3;
    map->add_layer();
    map->add_layer();
    return true;
  }

  void look_at(float x, float y, float zoom = 1.0f) {
    cam->local_transform().position = {x, y};
    static_cast<arcane::Camera*>(cam->find_component("Camera"))->zoom = zoom;
  }

  // Renders into the bound FBO and classifies the pixel at screen position (x, y), y down.
  Px at(arcane::SpriteRenderer& renderer, int x, int y) {
    return classify(renderer.read_pixel(x, kSize - 1 - y));
  }
  void render(arcane::SpriteRenderer& renderer) {
    arcane::editor::render_scene(scene, renderer, textures, kSize, kSize, nullptr, nullptr, nullptr, &tilesets);
  }
};

}  // namespace

int main() {
  auto window = arcane::Window::create({.width = kSize, .height = kSize, .title = "test_tilemap_gl", .hidden = true});
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

  Fixture f;
  ARCANE_ASSERT_TRUE(f.init());

  // Tile placement: layer 0 has red at cell (0,0) and blue at (2,1); the rest is empty.
  f.map->set_tile(0, 0, 0, 1);
  f.map->set_tile(0, 2, 1, 2);
  f.look_at(32.0f, 32.0f);  // view = world [0,64)^2
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 8, 8) == Px::Red);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 40, 24) == Px::Blue);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 24, 8) == Px::Background);   // empty cell (1,0)
  ARCANE_ASSERT_TRUE(f.at(*renderer, 56, 8) == Px::Background);   // beyond the 3-wide grid
  ARCANE_ASSERT_TRUE(f.at(*renderer, 8, 56) == Px::Background);   // beyond the 3-high grid
  ARCANE_ASSERT_TRUE(f.at(*renderer, 40, 40) == Px::Background);  // empty cell (2,2)

  // Layer order: layer 1 draws over layer 0.
  f.map->set_tile(1, 0, 0, 2);
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 8, 8) == Px::Blue);
  f.map->set_tile(1, 0, 0, 0);  // erased top layer reveals the one below again
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 8, 8) == Px::Red);

  // Camera edge: view = world [8,72) x [0,64); cell 0 is half visible, and the grid ends at x=48 (screen 40).
  f.look_at(40.0f, 32.0f);
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 2, 8) == Px::Red);         // world x=10, still inside cell (0,0)
  ARCANE_ASSERT_TRUE(f.at(*renderer, 36, 24) == Px::Blue);      // world x=44, cell (2,1)
  ARCANE_ASSERT_TRUE(f.at(*renderer, 44, 24) == Px::Background);  // world x=52, past the grid

  // Camera fully past the left edge of the map: nothing drawn.
  f.look_at(-100.0f, 32.0f);
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 8, 8) == Px::Background);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 56, 56) == Px::Background);

  // Zoom 2 centred on cell (2,1): view = world [32,64) x [16,48), so cell (2,1) is blue at screen (0..32, 0..32).
  f.look_at(48.0f, 32.0f, 2.0f);
  f.render(*renderer);
  ARCANE_ASSERT_TRUE(f.at(*renderer, 16, 16) == Px::Blue);   // world (40,24) in cell (2,1)
  ARCANE_ASSERT_TRUE(f.at(*renderer, 16, 48) == Px::Background);  // world (40,40) = empty cell (2,2)

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &color);
  std::filesystem::remove_all(f.dir);
  return 0;
}
