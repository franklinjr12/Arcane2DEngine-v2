#include <cmath>
#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "arcane/tilemap.hpp"
#include "test_assert.hpp"

int main() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  arcane::register_tilemap_components(registry);
  arcane::Scene scene;
  auto* node = scene.create_node("camera");
  auto* target = scene.create_node("target");
  auto* camera = static_cast<arcane::Camera2D*>(scene.add_component(node, "Camera2D", registry));
  camera->target = target->id();
  camera->deadzone = {2, 3};
  target->local_transform().position = {10, 2};
  scene.update(0.1f);
  auto center = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(center.x == 8 && center.y == 0);
  camera->smoothing = 1;
  target->local_transform().position.x = 20;
  scene.update(1);
  center = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(center.x > 8 && center.x < 18);
  camera->bounds_enabled = true;
  camera->bounds_min = {0, 0}; camera->bounds_max = {100, 100};
  camera->zoom = 2;
  center = camera->view_center(40, 40);
  ARCANE_ASSERT_TRUE(center.x >= 10 && center.y == 10);
  camera->bounds_enabled = false; camera->pixel_snap = true;
  center = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(std::round(center.x * 2) == center.x * 2);
  camera->pixel_snap = false; camera->target = 0;
  camera->shake_seed = 42; camera->shake_amplitude = 10; camera->shake_duration = 1;
  camera->start_shake(); scene.update(0.5f);
  const auto first = camera->view_center(0, 0);
  scene.update(0.5f); const auto second = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(std::abs(second.x) <= 5 && std::abs(second.y) <= 5);
  scene.update(0.5f); center = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(center.x == 0 && center.y == 0);
  camera->start_shake(); scene.update(0.5f); center = camera->view_center(0, 0);
  ARCANE_ASSERT_TRUE(center.x == first.x && center.y == first.y);
  const auto text = scene.save_to_json(registry);
  auto loaded = arcane::Scene::load_from_json(text, registry);
  ARCANE_ASSERT_TRUE(loaded.has_value());
  ARCANE_ASSERT_TRUE(loaded->save_to_json(registry) == text);
  auto* restored = static_cast<arcane::Camera2D*>(loaded->find_node(node->id())->find_component("Camera2D"));
  ARCANE_ASSERT_TRUE(restored->shake_seed == 42 && restored->shake_duration == 1 && restored->zoom == 2);
  loaded->update(0.5f); center = restored->view_center(0, 0);
  ARCANE_ASSERT_TRUE(center.x == first.x && center.y == first.y);

  {
    // Look-ahead leads the last movement direction; bounds come from a Tilemap node's world rectangle.
    arcane::Scene world;
    auto* cam_node = world.create_node("camera");
    auto* hero = world.create_node("hero");
    auto* level = world.create_node("level");
    auto* cam = static_cast<arcane::Camera2D*>(world.add_component(cam_node, "Camera2D", registry));
    auto* map = static_cast<arcane::Tilemap*>(world.add_component(level, "Tilemap", registry));
    map->tile_width = map->tile_height = 10; map->width = 20; map->height = 10;  // world [0,200] x [0,100]
    cam->target = hero->id();
    cam->look_ahead = {5, 0};
    hero->local_transform().position = {50, 50};
    world.update(0.1f);
    hero->local_transform().position = {60, 50};
    world.update(0.1f);
    auto c = cam->view_center(20, 20);
    ARCANE_ASSERT_TRUE(c.x == 65 && c.y == 50);
    hero->local_transform().position = {55, 50};
    world.update(0.1f);
    c = cam->view_center(20, 20);
    ARCANE_ASSERT_TRUE(c.x == 50 && c.y == 50);
    cam->bounds_tilemap = level->id();
    hero->local_transform().position = {2, 2};
    world.update(0.1f);
    c = cam->view_center(40, 40);  // half view 20 -> clamped to the tilemap's top-left
    ARCANE_ASSERT_TRUE(c.x == 20 && c.y == 20);
    hero->local_transform().position = {500, 500};
    world.update(0.1f);
    c = cam->view_center(40, 40);
    ARCANE_ASSERT_TRUE(c.x == 180 && c.y == 80);
    auto* layer = static_cast<arcane::ParallaxLayer*>(world.add_component(cam_node, "ParallaxLayer", registry));
    layer->factor = {0.25f, 0.0f};
    layer->repeat_x = true;
    auto reloaded = arcane::Scene::load_from_json(world.save_to_json(registry), registry);
    ARCANE_ASSERT_TRUE(reloaded.has_value());
    auto* back = static_cast<arcane::ParallaxLayer*>(reloaded->find_node(cam_node->id())->find_component("ParallaxLayer"));
    ARCANE_ASSERT_TRUE(back->factor.x == 0.25f && back->repeat_x && !back->repeat_y);
  }
}
