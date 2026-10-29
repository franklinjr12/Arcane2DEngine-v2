// Baseline section 17: a scene with a Sprite, a Camera and a module-defined
// component (Spinner from sample_game) is saved, closed, reopened and played;
// Play/Stop must leave the authored values exactly as the author set them.
#include <filesystem>

#include "arcane/builtin_components.hpp"
#include "arcane/component_registry.hpp"
#include "arcane/module.hpp"
#include "arcane/node.hpp"
#include "arcane/scene.hpp"
#include "arcane_editor/editor_state.hpp"
#include "arcane_editor/project_session.hpp"
#include "test_assert.hpp"

int main() {
  auto module = arcane::GameModule::load(ARCANE_SAMPLE_GAME_PATH);
  ARCANE_ASSERT_TRUE(module.has_value());
  arcane::ComponentRegistry registry;
  arcane::register_builtin_components(registry);
  module->api()->register_types(registry);

  const auto dir = std::filesystem::temp_directory_path() / "arcane_test_foundation_acceptance";
  std::filesystem::remove_all(dir);
  arcane::NodeId node_id = 0;
  std::string authored_json;
  {
    auto session = arcane::editor::ProjectSession::create(dir, "acceptance", registry);
    ARCANE_ASSERT_TRUE(session.has_value());
    arcane::Node* node = session->scene().create_node("Hero");
    node_id = node->id();
    ARCANE_ASSERT_TRUE(session->scene().add_component(node, "Sprite", registry) != nullptr);
    ARCANE_ASSERT_TRUE(session->scene().add_component(node, "Camera", registry) != nullptr);
    arcane::Component* spinner = session->scene().add_component(node, "Spinner", registry);
    ARCANE_ASSERT_TRUE(spinner != nullptr);
    for (const auto& d : *registry.properties_of("Spinner")) {
      if (d.name == "speed_degrees_per_second") d.set(*spinner, arcane::PropertyValue{45.0f});
    }
    session->mark_dirty();
    ARCANE_ASSERT_TRUE(session->save(registry).has_value());
  }  // session closed

  auto reopened = arcane::editor::ProjectSession::open(dir, registry);
  ARCANE_ASSERT_TRUE(reopened.has_value());
  arcane::Node* node = reopened->scene().find_node(node_id);
  ARCANE_ASSERT_TRUE(node != nullptr);
  ARCANE_ASSERT_TRUE(node->find_component("Sprite") != nullptr);
  ARCANE_ASSERT_TRUE(node->find_component("Camera") != nullptr);
  arcane::Component* spinner = node->find_component("Spinner");
  ARCANE_ASSERT_TRUE(spinner != nullptr);
  authored_json = reopened->scene().save_to_json(registry);

  // Play: the module's on_update really runs on the runtime copy only.
  arcane::editor::PlayStopController controller;
  ARCANE_ASSERT_TRUE(controller.play(reopened->scene(), registry));
  for (int i = 0; i < 60; ++i) controller.tick(1.0f / 60.0f);
  arcane::Node* runtime_node = controller.active_scene(reopened->scene())->find_node(node_id);
  ARCANE_ASSERT_TRUE(runtime_node != nullptr);
  ARCANE_ASSERT_TRUE(runtime_node->local_transform().rotation_radians != node->local_transform().rotation_radians);
  controller.stop();

  ARCANE_ASSERT_TRUE(reopened->scene().save_to_json(registry) == authored_json);
  ARCANE_ASSERT_TRUE(!reopened->dirty());

  std::filesystem::remove_all(dir);
  return 0;
}
