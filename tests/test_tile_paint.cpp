#include <filesystem>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/project_session.hpp"
#include "arcane_editor/scene_render.hpp"
#include "arcane_editor/tile_paint.hpp"
#include "test_assert.hpp"

namespace {

using arcane::Vec2;
using arcane::editor::TilePaintTool;

arcane::Tilemap make_map() {
  arcane::Tilemap map;
  map.width = 4;
  map.height = 3;
  map.tile_width = 16.0f;
  map.tile_height = 16.0f;
  map.add_layer();
  return map;
}

void test_screen_to_tile_under_pan_and_zoom() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::register_tilemap_components(registry);
  arcane::Scene scene;
  arcane::Node* cam = scene.create_node("cam");
  cam->local_transform().position = {40.0f, 24.0f};
  static_cast<arcane::Camera*>(scene.add_component(cam, "Camera", registry))->zoom = 2.0f;

  // 128x96 viewport at zoom 2 shows world [8,72) x [0,48): screen (0,0) = world (8,0).
  auto map = make_map();
  arcane::WorldTransform2D origin;
  TilePaintTool tool;
  tool.tile = 5;
  const Vec2 w = arcane::editor::viewport_to_world(scene, 40.0f, 40.0f, 128, 96);  // world (28, 20) -> cell (1,1)
  ARCANE_ASSERT_TRUE(tool.brush(map, origin, w, false));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 5);
  // Pixel just left of the map's world x=0 (screen x = -16 at this pan/zoom) is outside the grid.
  ARCANE_ASSERT_TRUE(!tool.brush(map, origin, arcane::editor::viewport_to_world(scene, -20.0f, 40.0f, 128, 96), false));
}

void test_brush_erase_and_bounds() {
  auto map = make_map();
  arcane::WorldTransform2D world;
  world.position = {100.0f, 50.0f};
  world.scale = {2.0f, 2.0f};  // cells are 32x32 in world space
  TilePaintTool tool;
  tool.tile = 2;
  ARCANE_ASSERT_TRUE(tool.brush(map, world, {100.0f + 40.0f, 50.0f + 70.0f}, false));  // cell (1,2)
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 2) == 2);
  ARCANE_ASSERT_TRUE(!tool.brush(map, world, {140.0f, 120.0f}, false));  // already 2
  ARCANE_ASSERT_TRUE(tool.brush(map, world, {140.0f, 120.0f}, true));    // erase
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 2) == 0);
  ARCANE_ASSERT_TRUE(!tool.brush(map, world, {99.0f, 60.0f}, false));    // left of the map
  ARCANE_ASSERT_TRUE(!tool.brush(map, world, {100.0f + 4 * 32.0f, 60.0f}, false));  // right of the last column
  tool.layer = 1;
  ARCANE_ASSERT_TRUE(!tool.brush(map, world, {110.0f, 60.0f}, false));  // no such layer
}

void test_fill_rect() {
  auto map = make_map();
  arcane::WorldTransform2D world;
  TilePaintTool tool;
  tool.tile = 3;
  // Corners given in reverse order; (1,0)..(2,1) -> 4 cells.
  ARCANE_ASSERT_TRUE(tool.fill_rect(map, world, {40.0f, 20.0f}, {20.0f, 2.0f}, false));
  for (int cy = 0; cy < 3; ++cy)
    for (int cx = 0; cx < 4; ++cx)
      ARCANE_ASSERT_TRUE(map.tile_at(0, cx, cy) == ((cx == 1 || cx == 2) && cy <= 1 ? 3 : 0));
  ARCANE_ASSERT_TRUE(!tool.fill_rect(map, world, {40.0f, 20.0f}, {20.0f, 2.0f}, false));  // nothing new
  // Rectangle partly outside the grid is clamped; entirely outside does nothing.
  ARCANE_ASSERT_TRUE(tool.fill_rect(map, world, {-50.0f, -50.0f}, {10.0f, 10.0f}, false));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 0, 0) == 3);
  ARCANE_ASSERT_TRUE(!tool.fill_rect(map, world, {-50.0f, -50.0f}, {-10.0f, -10.0f}, false));
  ARCANE_ASSERT_TRUE(!tool.fill_rect(map, world, {500.0f, 500.0f}, {600.0f, 600.0f}, false));
  // Erase rect.
  ARCANE_ASSERT_TRUE(tool.fill_rect(map, world, {0.0f, 0.0f}, {63.0f, 47.0f}, true));
  for (int cy = 0; cy < 3; ++cy)
    for (int cx = 0; cx < 4; ++cx) ARCANE_ASSERT_TRUE(map.tile_at(0, cx, cy) == 0);
}

void test_undo_redo_strokes() {
  auto map = make_map();
  arcane::WorldTransform2D world;
  TilePaintTool tool;
  tool.tile = 1;
  ARCANE_ASSERT_TRUE(!tool.can_undo() && !tool.undo(map));

  tool.begin_stroke();  // one drag over 3 cells = one undo step
  tool.begin_stroke();  // repeated each frame by the panel: must not split the stroke
  tool.brush(map, world, {2.0f, 2.0f}, false);
  tool.brush(map, world, {18.0f, 2.0f}, false);
  tool.brush(map, world, {34.0f, 2.0f}, false);
  tool.end_stroke();
  tool.fill_rect(map, world, {0.0f, 16.0f}, {63.0f, 40.0f}, false);  // separate step, outside a stroke

  ARCANE_ASSERT_TRUE(tool.undo(map));  // the fill
  ARCANE_ASSERT_TRUE(map.tile_at(0, 0, 1) == 0 && map.tile_at(0, 3, 2) == 0 && map.tile_at(0, 2, 0) == 1);
  ARCANE_ASSERT_TRUE(tool.undo(map));  // the whole drag
  ARCANE_ASSERT_TRUE(map.tile_at(0, 0, 0) == 0 && map.tile_at(0, 1, 0) == 0 && map.tile_at(0, 2, 0) == 0);
  ARCANE_ASSERT_TRUE(!tool.can_undo() && tool.can_redo());
  ARCANE_ASSERT_TRUE(tool.redo(map));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 2, 0) == 1 && map.tile_at(0, 0, 1) == 0);
  // A new edit clears the redo history.
  tool.brush(map, world, {50.0f, 40.0f}, false);
  ARCANE_ASSERT_TRUE(!tool.can_redo());

  // Overpainting restores the previous tile, not 0.
  tool.tile = 4;
  tool.brush(map, world, {2.0f, 2.0f}, false);
  ARCANE_ASSERT_TRUE(map.tile_at(0, 0, 0) == 4);
  tool.undo(map);
  ARCANE_ASSERT_TRUE(map.tile_at(0, 0, 0) == 1);
}

// The panel marks the session dirty exactly when a tool call reports a change.
void test_dirty_flag_follows_changes() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::register_tilemap_components(registry);
  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_tile_paint_proj";
  std::filesystem::remove_all(dir);
  auto session = arcane::editor::ProjectSession::create(dir, "paint", registry);
  ARCANE_ASSERT_TRUE(session.has_value());
  ARCANE_ASSERT_TRUE(!session->dirty());

  arcane::Node* node = session->scene().create_node("map");
  auto* map = static_cast<arcane::Tilemap*>(session->scene().add_component(node, "Tilemap", registry));
  map->add_layer();
  TilePaintTool tool;
  if (tool.brush(*map, node->world_transform(), {-5.0f, -5.0f}, false)) session->mark_dirty();
  ARCANE_ASSERT_TRUE(!session->dirty());  // outside the grid: no change, stays clean
  if (tool.brush(*map, node->world_transform(), {5.0f, 5.0f}, false)) session->mark_dirty();
  ARCANE_ASSERT_TRUE(session->dirty());
  ARCANE_ASSERT_TRUE(session->save(registry).has_value());
  ARCANE_ASSERT_TRUE(!session->dirty());
  if (tool.undo(*map)) session->mark_dirty();
  ARCANE_ASSERT_TRUE(session->dirty() && map->tile_at(0, 0, 0) == 0);
  std::filesystem::remove_all(dir);
}

void test_terrain_paint() {
  arcane::Tileset set; set.tile_count = 16; set.tiles.resize(16);
  arcane::TerrainRule rule; rule.terrain = 1;
  for (int i = 0; i < 16; ++i) { set.tiles[i].terrain = 1; rule.tiles[i] = i + 1; }
  set.terrains.push_back(rule);
  arcane::Tilemap map; map.width = 3; map.height = 3; map.add_layer();
  arcane::WorldTransform2D w; TilePaintTool tool;
  ARCANE_ASSERT_TRUE(tool.terrain_brush(map, set, w, {33, 33}, 1, false));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 1);
  ARCANE_ASSERT_TRUE(tool.terrain_brush(map, set, w, {65, 33}, 1, false));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 3 && map.tile_at(0, 2, 1) == 9);
  ARCANE_ASSERT_TRUE(TilePaintTool::terrain_mask(map, set, 0, 1, 1, 1) == 2);
  ARCANE_ASSERT_TRUE(tool.terrain_brush(map, set, w, {65, 33}, 1, true));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 1 && map.tile_at(0, 2, 1) == 0);
  ARCANE_ASSERT_TRUE(tool.undo(map));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 3 && map.tile_at(0, 2, 1) == 9);
  ARCANE_ASSERT_TRUE(tool.redo(map));
  ARCANE_ASSERT_TRUE(map.tile_at(0, 1, 1) == 1 && map.tile_at(0, 2, 1) == 0);
}

}  // namespace

int main() {
  test_terrain_paint();
  test_screen_to_tile_under_pan_and_zoom();
  test_brush_erase_and_bounds();
  test_fill_rect();
  test_undo_redo_strokes();
  test_dirty_flag_follows_changes();
  return 0;
}
