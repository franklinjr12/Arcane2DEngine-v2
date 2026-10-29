#include <cmath>
#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/scene.hpp"
#include "test_assert.hpp"

int main() {
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
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
}
