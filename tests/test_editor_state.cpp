#include <filesystem>
#include <fstream>
#include <memory>

#include "arcane/component.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/node.hpp"
#include "arcane/physics.hpp"
#include "arcane/scene.hpp"
#include "arcane/scene_flow.hpp"
#include "arcane_editor/editor_state.hpp"
#include "test_assert.hpp"

namespace {

class Counter : public arcane::Component {
 public:
  int value = 0;
};

arcane::ComponentRegistry make_registry() {
  arcane::ComponentRegistry registry;
  registry.register_component(
      "Counter", [] { return std::make_unique<Counter>(); },
      {arcane::make_property<Counter>("value", arcane::PropertyType::Int, &Counter::value)});
  return registry;
}

// Core invariant from docs/decisions.md: Play snapshots the authored scene
// into a temporary runtime scene; mutations during Play must not leak back
// into the authored scene after Stop.
void test_play_stop_isolation() {
  arcane::ComponentRegistry registry = make_registry();
  arcane::Scene authored;
  arcane::Node* node = authored.create_node("root");
  auto* counter = static_cast<Counter*>(authored.add_component(node, "Counter", registry));
  counter->value = 1;

  arcane::editor::PlayStopController controller;
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Edit);
  ARCANE_ASSERT_TRUE(controller.play(authored, registry));
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Play);

  arcane::Scene* runtime = controller.active_scene(authored);
  ARCANE_ASSERT_TRUE(runtime != &authored);
  auto* runtime_counter = static_cast<Counter*>(runtime->roots()[0]->components()[0].get());
  runtime_counter->value = 99;

  controller.stop();
  ARCANE_ASSERT_TRUE(controller.mode() == arcane::editor::EditorMode::Edit);
  ARCANE_ASSERT_TRUE(controller.active_scene(authored) == &authored);
  ARCANE_ASSERT_TRUE(counter->value == 1);
}

// Play drives the runtime scene's PhysicsWorld (a falling body actually
// falls), but Stop discards it entirely -- the authored scene's node
// position must be exactly what the author set, never a physics result.
void test_play_stop_isolation_with_physics() {
  arcane::ComponentRegistry registry;
  arcane::register_physics_components(registry);

  arcane::Scene authored;
  arcane::Node* node = authored.create_node("falling");
  node->local_transform().position = {0.0f, 320.0f};
  authored.add_component(node, "RigidBody2D", registry);
  authored.add_component(node, "Collider2D", registry);

  arcane::editor::PlayStopController controller;
  ARCANE_ASSERT_TRUE(controller.play(authored, registry));

  arcane::Scene* runtime = controller.active_scene(authored);
  for (int i = 0; i < 30; ++i) controller.tick(1.0f / 60.0f);

  arcane::Node* runtime_node = runtime->roots()[0].get();
  ARCANE_ASSERT_TRUE(runtime_node->local_transform().position.y > 320.0f);

  controller.stop();
  ARCANE_ASSERT_TRUE(controller.active_scene(authored) == &authored);
  ARCANE_ASSERT_TRUE(node->local_transform().position.y == 320.0f);
}

void add_body(arcane::Scene& scene, const char* name, const arcane::ComponentRegistry& registry) {
  arcane::Node* node = scene.create_node(name);
  scene.add_component(node, "RigidBody2D", registry);
  scene.add_component(node, "Collider2D", registry);
}

void write_scene(const std::filesystem::path& path, const arcane::Scene& scene, const arcane::ComponentRegistry& registry) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << scene.save_to_json(registry);
}

// Play owns a SceneManager: gameplay can request_switch, physics is rebuilt
// for the new scene, authored persistent roots keep their runtime state, and
// Stop clears the active manager without touching the authored scene.
void test_play_scene_manager() {
  namespace fs = std::filesystem;
  arcane::ComponentRegistry registry = make_registry();
  arcane::register_physics_components(registry);

  const fs::path dir = fs::temp_directory_path() / "arcane_editor_state_play_switch";
  std::error_code ec;
  fs::remove_all(dir, ec);

  arcane::Scene a;
  add_body(a, "a_body", registry);
  arcane::Node* hero = a.create_node("Hero");
  hero->set_persistent(true);
  static_cast<Counter*>(a.add_component(hero, "Counter", registry))->value = 5;
  a.add_component(hero, "RigidBody2D", registry);
  a.add_component(hero, "Collider2D", registry);
  arcane::Scene b;
  add_body(b, "b1", registry);
  add_body(b, "b2", registry);
  write_scene(dir / "scenes/a.scene.json", a, registry);
  write_scene(dir / "scenes/b.scene.json", b, registry);

  arcane::editor::PlayStopController controller;
  ARCANE_ASSERT_TRUE(controller.scene_manager() == nullptr);
  ARCANE_ASSERT_TRUE(arcane::active_scene_manager() == nullptr);
  ARCANE_ASSERT_TRUE(controller.play(a, registry, dir, {}, "scenes/a.scene.json"));
  arcane::SceneManager* manager = controller.scene_manager();
  ARCANE_ASSERT_TRUE(manager != nullptr && arcane::active_scene_manager() == manager);
  ARCANE_ASSERT_TRUE(manager->current_scene_path() == "scenes/a.scene.json");
  ARCANE_ASSERT_TRUE(controller.active_scene(a) == manager->scene() && manager->scene() != &a);
  ARCANE_ASSERT_TRUE(controller.physics_body_count() == 2);

  // Runtime state of the authored persistent root.
  arcane::Node* live_hero = manager->find_persistent("Hero");
  ARCANE_ASSERT_TRUE(live_hero != nullptr);
  static_cast<Counter*>(live_hero->components()[0].get())->value = 42;

  arcane::active_scene_manager()->request_switch("scenes/b.scene.json", false);
  controller.tick(1.0f / 60.0f);
  arcane::Scene* now = controller.active_scene(a);
  ARCANE_ASSERT_TRUE(now == manager->scene());
  ARCANE_ASSERT_TRUE(manager->current_scene_path() == "scenes/b.scene.json");
  ARCANE_ASSERT_TRUE(manager->last_error().empty());
  ARCANE_ASSERT_TRUE(now->roots().size() == 3);  // b1, b2 and the carried Hero
  ARCANE_ASSERT_TRUE(controller.physics_body_count() == 3);
  arcane::Node* carried = manager->find_persistent("Hero");
  ARCANE_ASSERT_TRUE(carried != nullptr);
  ARCANE_ASSERT_TRUE(static_cast<Counter*>(carried->components()[0].get())->value == 42);
  controller.tick(1.0f / 60.0f);  // the rebuilt world keeps stepping

  controller.stop();
  ARCANE_ASSERT_TRUE(arcane::active_scene_manager() == nullptr);
  ARCANE_ASSERT_TRUE(controller.scene_manager() == nullptr);
  ARCANE_ASSERT_TRUE(controller.active_scene(a) == &a);
  ARCANE_ASSERT_TRUE(a.roots().size() == 2);
  ARCANE_ASSERT_TRUE(static_cast<Counter*>(hero->components()[0].get())->value == 5);
  fs::remove_all(dir, ec);
}

void test_selection() {
  arcane::editor::Selection selection;
  ARCANE_ASSERT_TRUE(!selection.has_selection());
  selection.select(arcane::NodeId{7});
  ARCANE_ASSERT_TRUE(selection.has_selection());
  ARCANE_ASSERT_TRUE(selection.selected().value() == arcane::NodeId{7});
  selection.clear();
  ARCANE_ASSERT_TRUE(!selection.has_selection());
}

}  // namespace

int main() {
  test_play_stop_isolation();
  test_play_stop_isolation_with_physics();
  test_play_scene_manager();
  test_selection();
  return 0;
}
