#include <memory>

#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

namespace {

void test_tileset_json_round_trip_and_uv() {
  arcane::Tileset t;
  t.texture = 77;
  t.tile_width = 16;
  t.tile_height = 16;
  t.columns = 4;
  t.tile_count = 8;
  auto loaded = arcane::load_tileset_from_json(arcane::save_tileset_to_json(t));
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->texture == 77 && loaded->columns == 4 && loaded->tile_count == 8);

  const auto uv = arcane::tile_uv(*loaded, 6);  // index 5 -> col 1, row 1 of a 4x2 grid
  ARCANE_ASSERT_TRUE(uv.u0 == 0.25f && uv.u1 == 0.5f && uv.v0 == 0.5f && uv.v1 == 1.0f);
  ARCANE_ASSERT_TRUE(arcane::tile_uv(*loaded, 0).u1 == 1.0f);  // invalid id -> full texture

  ARCANE_ASSERT_TRUE(!arcane::load_tileset_from_json("{\"format_version\":2}").has_value());
  ARCANE_ASSERT_TRUE(!arcane::load_tileset_from_json("{\"format_version\":1,\"texture\":1,\"tile_width\":0,"
                                                      "\"tile_height\":1,\"columns\":1,\"tile_count\":1}")
                          .has_value());
}

void test_cell_world_coordinates() {
  arcane::Tilemap map;
  map.tile_width = 32.0f;
  map.tile_height = 16.0f;
  map.width = 4;
  map.height = 3;

  int cx = -1, cy = -1;
  ARCANE_ASSERT_TRUE(map.local_to_cell({33.0f, 17.0f}, cx, cy) && cx == 1 && cy == 1);
  ARCANE_ASSERT_TRUE(!map.local_to_cell({-0.5f, 0.0f}, cx, cy));
  ARCANE_ASSERT_TRUE(!map.local_to_cell({128.0f, 0.0f}, cx, cy));  // width*32 is one past the edge
  ARCANE_ASSERT_TRUE(map.cell_to_local(2, 1) == (arcane::Vec2{64.0f, 16.0f}));

  arcane::WorldTransform2D world;
  world.position = {100.0f, 50.0f};
  world.scale = {2.0f, 2.0f};
  ARCANE_ASSERT_TRUE(map.world_to_cell(world, {100.0f + 64.0f + 1.0f, 50.0f + 32.0f + 1.0f}, cx, cy) && cx == 1 &&
                      cy == 1);
}

void test_layers_serialization_through_scene() {
  arcane::ComponentRegistry registry;
  arcane::register_tilemap_components(registry);

  arcane::Scene scene;
  arcane::Node* node = scene.create_node("map");
  auto* map = static_cast<arcane::Tilemap*>(scene.add_component(node, "Tilemap", registry));
  map->width = 3;
  map->height = 2;
  map->tileset = 9;
  map->collide = true;
  map->solid_tiles = "1,2";
  map->add_layer();
  map->add_layer();
  ARCANE_ASSERT_TRUE(map->set_tile(0, 2, 1, 5));
  ARCANE_ASSERT_TRUE(map->set_tile(1, 0, 0, 3));
  ARCANE_ASSERT_TRUE(!map->set_tile(2, 0, 0, 1));
  ARCANE_ASSERT_TRUE(!map->set_tile(0, 3, 0, 1));

  auto loaded = arcane::Scene::load_from_json(scene.save_to_json(registry), registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  auto* copy = static_cast<arcane::Tilemap*>(loaded->roots()[0]->find_component("Tilemap"));
  ARCANE_ASSERT_TRUE(copy != nullptr);
  ARCANE_ASSERT_TRUE(copy->layer_count() == 2 && copy->width == 3 && copy->height == 2);
  ARCANE_ASSERT_TRUE(copy->tileset == 9 && copy->collide && copy->solid_tiles == "1,2");
  ARCANE_ASSERT_TRUE(copy->tile_at(0, 2, 1) == 5 && copy->tile_at(1, 0, 0) == 3);
  ARCANE_ASSERT_TRUE(copy->tile_at(0, 0, 0) == 0 && copy->tile_at(0, 9, 9) == 0);
}

void test_solid_rects_merge_runs() {
  arcane::Tilemap map;
  map.width = 4;
  map.height = 2;
  map.tile_width = 10.0f;
  map.tile_height = 10.0f;
  map.solid_tiles = "1";
  map.add_layer();
  map.add_layer();
  for (int x = 0; x < 3; ++x) map.set_tile(0, x, 0, 1);
  map.set_tile(0, 3, 0, 2);  // not solid
  map.set_tile(1, 3, 1, 1);  // solid on another layer
  map.set_tile(0, 1, 1, 1);

  const auto rects = map.solid_rects();
  ARCANE_ASSERT_TRUE(rects.size() == 3);
  ARCANE_ASSERT_TRUE(rects[0].x == 0.0f && rects[0].y == 0.0f && rects[0].width == 30.0f && rects[0].height == 10.0f);
  ARCANE_ASSERT_TRUE(rects[1].x == 10.0f && rects[1].y == 10.0f && rects[1].width == 10.0f);
  ARCANE_ASSERT_TRUE(rects[2].x == 30.0f && rects[2].y == 10.0f);
}

void test_tilemap_collider_stops_falling_body() {
  arcane::ComponentRegistry registry;
  arcane::register_tilemap_components(registry);
  arcane::register_physics_components(registry);
  arcane::Scene scene;

  arcane::Node* node = scene.create_node("ground");
  node->local_transform().position = {0.0f, -32.0f};
  auto* map = static_cast<arcane::Tilemap*>(scene.add_component(node, "Tilemap", registry));
  map->width = 10;
  map->height = 1;
  map->collide = true;
  map->solid_tiles = "1";
  map->add_layer();
  for (int x = 0; x < 10; ++x) map->set_tile(0, x, 0, 1);

  arcane::Node* ball = scene.create_node("ball");
  ball->local_transform().position = {100.0f, 200.0f};
  scene.add_component(ball, "RigidBody2D", registry);
  auto* collider = static_cast<arcane::Collider2D*>(scene.add_component(ball, "Collider2D", registry));
  collider->width = 16.0f;
  collider->height = 16.0f;

  arcane::PhysicsWorld world;
  world.rebuild(scene);
  for (int i = 0; i < 240; ++i) world.step(scene, 1.0f / 60.0f);

  // Physics is passed through as-is (y-up, see decisions.md row 3): the tile box spans y in [-32, 0] around the
  // node origin at y=-32, so a 16 px ball comes to rest with its center near y=8 instead of free-falling.
  const float y = ball->local_transform().position.y;
  ARCANE_ASSERT_TRUE(y > 0.0f && y < 20.0f);
}

}  // namespace

int main() {
  test_tileset_json_round_trip_and_uv();
  test_cell_world_coordinates();
  test_layers_serialization_through_scene();
  test_solid_rects_merge_runs();
  test_tilemap_collider_stops_falling_body();
  return 0;
}
