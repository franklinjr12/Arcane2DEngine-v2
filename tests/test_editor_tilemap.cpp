#include <fstream>
#include <filesystem>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "arcane_editor/asset_catalog.hpp"
#include "arcane_editor/scene_render.hpp"
#include "test_assert.hpp"

namespace {

void test_viewport_to_world_uses_primary_camera() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::Scene scene;
  arcane::Node* cam = scene.create_node("cam");
  cam->local_transform().position = {100.0f, 50.0f};
  static_cast<arcane::Camera*>(scene.add_component(cam, "Camera", registry))->zoom = 2.0f;

  // Viewport center maps to the camera position; 20 px right = 10 world px at zoom 2.
  auto center = arcane::editor::viewport_to_world(scene, 200.0f, 100.0f, 400, 200);
  ARCANE_ASSERT_TRUE(center == (arcane::Vec2{100.0f, 50.0f}));
  auto right = arcane::editor::viewport_to_world(scene, 220.0f, 100.0f, 400, 200);
  ARCANE_ASSERT_TRUE(right == (arcane::Vec2{110.0f, 50.0f}));
}

void test_paint_and_erase() {
  arcane::Tilemap map;
  map.width = 4;
  map.height = 4;
  map.add_layer();
  arcane::WorldTransform2D world;
  world.position = {10.0f, 10.0f};

  using arcane::editor::paint_tile_at_world;
  ARCANE_ASSERT_TRUE(paint_tile_at_world(map, world, {10.0f + 40.0f, 10.0f + 70.0f}, 0, 3));  // cell (1, 2)
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 2) == 3);
  ARCANE_ASSERT_TRUE(!paint_tile_at_world(map, world, {50.0f, 80.0f}, 0, 3));  // unchanged
  ARCANE_ASSERT_TRUE(paint_tile_at_world(map, world, {50.0f, 80.0f}, 0, 0));   // erase
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 2) == 0);
  ARCANE_ASSERT_TRUE(!paint_tile_at_world(map, world, {0.0f, 0.0f}, 0, 1));    // outside the grid
  ARCANE_ASSERT_TRUE(!paint_tile_at_world(map, world, {50.0f, 80.0f}, 1, 1));  // no such layer
}

void test_tileset_cache_loads_json_asset() {
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_tileset_cache";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "tiles.json");
    arcane::Tileset t;
    t.texture = 5;
    t.columns = 2;
    t.tile_count = 4;
    out << arcane::save_tileset_to_json(t);
  }
  arcane::editor::TilesetCache cache;
  cache.set_assets(arcane::editor::list_animation_clip_assets(dir));
  const auto id = arcane::editor::asset_id_for_relative_path("tiles.json");
  const arcane::Tileset* loaded = cache.get(id);
  ARCANE_ASSERT_TRUE(loaded != nullptr && loaded->texture == 5 && loaded->tile_count == 4);
  ARCANE_ASSERT_TRUE(cache.get(12345) == nullptr);
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  test_viewport_to_world_uses_primary_camera();
  test_paint_and_erase();
  test_tileset_cache_loads_json_asset();
  return 0;
}
